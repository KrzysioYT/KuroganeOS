#include "../../runtime/user.h"

#include <kurogane/dns_service.h>
#include <kurogane/network.h>

#define DNSD_MAX_CLIENTS 8U

typedef struct dns_client {
    ku_service_connection_t connection;
    uint64_t pid;
    ku_dns_service_request request;
    ku_dns_service_response response;
    int request_pending;
    int response_ready;
    int active;
} dns_client;

static dns_client clients[DNSD_MAX_CLIENTS];
static size_t resolver_cursor;

static void clear_bytes(void* destination, size_t size) {
    uint8_t* bytes = (uint8_t*)destination;
    size_t index;
    for (index = 0U; index < size; ++index) bytes[index] = 0U;
}

static void reset_client(dns_client* client) {
    if (client == (dns_client*)0) return;
    clear_bytes(client, sizeof(*client));
}

static void disconnect_client(dns_client* client) {
    if (client == (dns_client*)0 || !client->active) return;
    (void)ku_service_close(client->connection);
    reset_client(client);
}

static int valid_host(const char host[KU_DNS_SERVICE_HOST_CAPACITY]) {
    size_t index;
    if (host[0] == '\0') return 0;
    for (index = 0U; index < KU_DNS_SERVICE_HOST_CAPACITY; ++index) {
        if (host[index] == '\0') return 1;
    }
    return 0;
}

static void prepare_error(
    dns_client* client,
    uint64_t request_id,
    ku_status_t status) {
    clear_bytes(&client->response, sizeof(client->response));
    client->response.structure_size = sizeof(client->response);
    client->response.status = status;
    client->response.request_id = request_id;
    client->response_ready = 1;
    client->request_pending = 0;
}

static void accept_clients(ku_service_endpoint_t endpoint) {
    for (;;) {
        const ku_result_t accepted = ku_service_accept(endpoint);
        size_t index;
        if (accepted == KU_STATUS_WOULD_BLOCK) return;
        if (accepted <= 0) return;
        for (index = 0U; index < DNSD_MAX_CLIENTS; ++index) {
            if (clients[index].active) continue;
            reset_client(&clients[index]);
            clients[index].connection = (ku_service_connection_t)accepted;
            clients[index].active = 1;
            break;
        }
        if (index == DNSD_MAX_CLIENTS) {
            (void)ku_service_close((ku_service_connection_t)accepted);
            return;
        }
    }
}

static void receive_requests(void) {
    size_t index;
    for (index = 0U; index < DNSD_MAX_CLIENTS; ++index) {
        dns_client* client = &clients[index];
        ku_service_message message;
        const ku_dns_service_request* request;
        ku_status_t status;
        if (!client->active || client->request_pending || client->response_ready) continue;
        status = ku_service_receive(client->connection, &message);
        if (status == KU_STATUS_WOULD_BLOCK) continue;
        if (status != KU_STATUS_OK) {
            disconnect_client(client);
            continue;
        }
        if (client->pid == 0U) client->pid = message.sender_pid;
        else if (client->pid != message.sender_pid) {
            prepare_error(client, 0U, KU_STATUS_ACCESS_DENIED);
            continue;
        }
        if (message.data_size != sizeof(ku_dns_service_request)) {
            prepare_error(client, 0U, KU_STATUS_CORRUPT_DATA);
            continue;
        }
        request = (const ku_dns_service_request*)(const void*)message.data;
        if (request->structure_size != sizeof(*request) ||
            request->request_id == 0U || request->flags != KU_DNS_SERVICE_FLAG_NONE ||
            request->reserved != 0U) {
            prepare_error(client, request->request_id, KU_STATUS_INVALID_ARGUMENT);
            continue;
        }
        if (request->operation != KU_DNS_SERVICE_RESOLVE_A) {
            prepare_error(client, request->request_id, KU_STATUS_NOT_SUPPORTED);
            continue;
        }
        if (!valid_host(request->host)) {
            prepare_error(client, request->request_id, KU_STATUS_INVALID_ARGUMENT);
            continue;
        }
        client->request = *request;
        client->request_pending = 1;
    }
}

static void resolve_one(void) {
    size_t attempt;
    for (attempt = 0U; attempt < DNSD_MAX_CLIENTS; ++attempt) {
        const size_t index = (resolver_cursor + attempt) % DNSD_MAX_CLIENTS;
        dns_client* client = &clients[index];
        ku_dns_a_request kernel_request;
        size_t host_index;
        ku_status_t status;
        if (!client->active || !client->request_pending || client->response_ready) continue;

        clear_bytes(&kernel_request, sizeof(kernel_request));
        kernel_request.structure_size = sizeof(kernel_request);
        kernel_request.flags = KU_DNS_FLAG_NONE;
        for (host_index = 0U; host_index < KU_DNS_NAME_CAPACITY; ++host_index) {
            kernel_request.host[host_index] = client->request.host[host_index];
            if (client->request.host[host_index] == '\0') break;
        }

        status = ku_dns_resolve_a(&kernel_request);
        clear_bytes(&client->response, sizeof(client->response));
        client->response.structure_size = sizeof(client->response);
        client->response.status = status;
        client->response.request_id = client->request.request_id;
        if (status == KU_STATUS_OK) {
            client->response.address[0] = kernel_request.address[0];
            client->response.address[1] = kernel_request.address[1];
            client->response.address[2] = kernel_request.address[2];
            client->response.address[3] = kernel_request.address[3];
        }
        client->response_ready = 1;
        client->request_pending = 0;
        resolver_cursor = (index + 1U) % DNSD_MAX_CLIENTS;
        return;
    }
}

static void flush_responses(void) {
    size_t index;
    for (index = 0U; index < DNSD_MAX_CLIENTS; ++index) {
        dns_client* client = &clients[index];
        ku_status_t status;
        if (!client->active || !client->response_ready) continue;
        status = ku_service_send(
            client->connection, &client->response, sizeof(client->response));
        if (status == KU_STATUS_WOULD_BLOCK) continue;
        if (status != KU_STATUS_OK) {
            disconnect_client(client);
            continue;
        }
        clear_bytes(&client->request, sizeof(client->request));
        clear_bytes(&client->response, sizeof(client->response));
        client->response_ready = 0;
    }
}

__attribute__((noreturn)) void _start(void) {
    ku_service_descriptor descriptor;
    ku_result_t endpoint;
    clear_bytes(&descriptor, sizeof(descriptor));
    descriptor.structure_size = sizeof(descriptor);
    descriptor.abi_version = KU_SERVICE_DESCRIPTOR_ABI_VERSION;
    descriptor.service_version = 1U;
    descriptor.minimum_client_version = 1U;
    descriptor.capabilities = UINT64_C(1);
    endpoint = ku_service_register_versioned(
        KU_DNS_SERVICE_NAME, KU_DNS_SERVICE_NAME_SIZE, &descriptor);
    if (endpoint <= 0) ku_exit(1);
    (void)u_puts("dnsd: dnsd.v1 async resolver online\n");
    for (;;) {
        accept_clients((ku_service_endpoint_t)endpoint);
        receive_requests();
        resolve_one();
        flush_responses();
        (void)ku_sleep(1U);
        (void)ku_yield();
    }
}

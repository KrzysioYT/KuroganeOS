#include "../../runtime/user.h"

#include <kurogane/clipboard.h>

#define CLIPBOARDD_MAX_CLIENTS 8U

typedef struct clipboardd_client {
    ku_service_connection_t connection;
    uint64_t pid;
    int active;
} clipboardd_client;

typedef struct clipboardd_state {
    uint32_t format;
    uint32_t data_size;
    uint64_t generation;
    uint64_t owner_pid;
    uint8_t data[KU_CLIPBOARD_DATA_CAPACITY];
} clipboardd_state;

static clipboardd_client clients[CLIPBOARDD_MAX_CLIENTS];
static clipboardd_state clipboard;

static void clear_bytes(void* destination, size_t size) {
    uint8_t* bytes = (uint8_t*)destination;
    size_t index;
    for (index = 0U; index < size; ++index) bytes[index] = 0U;
}

static void copy_bytes(void* destination, const void* source, size_t size) {
    uint8_t* output = (uint8_t*)destination;
    const uint8_t* input = (const uint8_t*)source;
    size_t index;
    for (index = 0U; index < size; ++index) output[index] = input[index];
}

static uint64_t next_generation(uint64_t generation) {
    ++generation;
    return generation == 0U ? UINT64_C(1) : generation;
}

static int payload_valid(uint32_t format, uint32_t data_size, const uint8_t* data) {
    if (data == (const uint8_t*)0 || data_size == 0U ||
        data_size > KU_CLIPBOARD_DATA_CAPACITY) return 0;
    switch (format) {
        case KU_CLIPBOARD_FORMAT_UTF8:
            return data[data_size - 1U] == 0U;
        case KU_CLIPBOARD_FORMAT_BINARY:
            return 1;
        default:
            return 0;
    }
}

static ku_status_t send_response(
    ku_service_connection_t connection,
    ku_status_t status) {
    ku_clipboard_response response;
    clear_bytes(&response, sizeof(response));
    response.structure_size = sizeof(response);
    response.status = status;
    response.format = clipboard.format;
    response.data_size = clipboard.data_size;
    response.generation = clipboard.generation;
    response.owner_pid = clipboard.owner_pid;
    if (clipboard.data_size != 0U)
        copy_bytes(response.data, clipboard.data, clipboard.data_size);
    return ku_service_send(connection, &response, sizeof(response));
}

static void set_clipboard(const ku_clipboard_request* request, uint64_t owner_pid) {
    const uint64_t generation = next_generation(clipboard.generation);
    clear_bytes(&clipboard, sizeof(clipboard));
    clipboard.format = request->format;
    clipboard.data_size = request->data_size;
    clipboard.generation = generation;
    clipboard.owner_pid = owner_pid;
    copy_bytes(clipboard.data, request->data, request->data_size);
}

static void clear_clipboard(void) {
    const uint64_t generation = next_generation(clipboard.generation);
    clear_bytes(&clipboard, sizeof(clipboard));
    clipboard.generation = generation;
}

static void handle_request(
    clipboardd_client* client,
    const ku_service_message* message) {
    const ku_clipboard_request* request;
    ku_status_t status = KU_STATUS_INVALID_ARGUMENT;

    if (message->data_size != sizeof(ku_clipboard_request)) {
        (void)send_response(client->connection, KU_STATUS_CORRUPT_DATA);
        return;
    }
    request = (const ku_clipboard_request*)(const void*)message->data;
    if (request->structure_size != sizeof(*request)) {
        (void)send_response(client->connection, KU_STATUS_INVALID_ARGUMENT);
        return;
    }
    if (client->pid == 0U) client->pid = message->sender_pid;
    else if (client->pid != message->sender_pid) {
        (void)send_response(client->connection, KU_STATUS_ACCESS_DENIED);
        return;
    }

    switch (request->operation) {
        case KU_CLIPBOARD_GET:
            if (request->format != KU_CLIPBOARD_FORMAT_NONE ||
                request->data_size != 0U || request->generation != 0U) {
                status = KU_STATUS_INVALID_ARGUMENT;
            } else {
                status = clipboard.format == KU_CLIPBOARD_FORMAT_NONE
                    ? KU_STATUS_NOT_FOUND
                    : KU_STATUS_OK;
            }
            break;
        case KU_CLIPBOARD_SET:
            if (!payload_valid(request->format, request->data_size, request->data)) {
                status = KU_STATUS_INVALID_ARGUMENT;
            } else {
                set_clipboard(request, message->sender_pid);
                status = KU_STATUS_OK;
            }
            break;
        case KU_CLIPBOARD_CLEAR:
            if (request->format != KU_CLIPBOARD_FORMAT_NONE || request->data_size != 0U) {
                status = KU_STATUS_INVALID_ARGUMENT;
            } else if (request->generation != 0U &&
                       request->generation != clipboard.generation) {
                status = KU_STATUS_ALREADY_EXISTS;
            } else {
                clear_clipboard();
                status = KU_STATUS_OK;
            }
            break;
        default:
            status = KU_STATUS_NOT_SUPPORTED;
            break;
    }
    (void)send_response(client->connection, status);
}

static void accept_clients(ku_service_endpoint_t endpoint) {
    for (;;) {
        const ku_result_t accepted = ku_service_accept(endpoint);
        size_t index;
        if (accepted == KU_STATUS_WOULD_BLOCK) return;
        if (accepted <= 0) return;
        for (index = 0U; index < CLIPBOARDD_MAX_CLIENTS; ++index) {
            if (!clients[index].active) {
                clients[index].connection = (ku_service_connection_t)accepted;
                clients[index].pid = 0U;
                clients[index].active = 1;
                break;
            }
        }
        if (index == CLIPBOARDD_MAX_CLIENTS) {
            (void)ku_service_close((ku_service_connection_t)accepted);
            return;
        }
    }
}

static void service_clients(void) {
    size_t index;
    for (index = 0U; index < CLIPBOARDD_MAX_CLIENTS; ++index) {
        clipboardd_client* client = &clients[index];
        ku_service_message message;
        ku_status_t status;
        if (!client->active) continue;
        status = ku_service_receive(client->connection, &message);
        if (status == KU_STATUS_WOULD_BLOCK) continue;
        if (status != KU_STATUS_OK) {
            (void)ku_service_close(client->connection);
            clear_bytes(client, sizeof(*client));
            continue;
        }
        handle_request(client, &message);
    }
}

__attribute__((noreturn)) void _start(void) {
    const ku_result_t endpoint = ku_service_register(
        KU_CLIPBOARD_SERVICE_NAME,
        KU_CLIPBOARD_SERVICE_NAME_SIZE);
    clear_bytes(&clipboard, sizeof(clipboard));
    if (endpoint <= 0) {
        (void)u_puts("clipboardd: service registration failed\n");
        (void)u_puts("[TEST] clipboard_service_online: FAIL\n");
        ku_exit(1);
    }
    (void)u_puts("clipboardd: clipboard.v1 online\n");
    (void)u_puts("[TEST] clipboard_service_online: PASS\n");
    for (;;) {
        accept_clients((ku_service_endpoint_t)endpoint);
        service_clients();
        (void)ku_sleep(1U);
    }
}

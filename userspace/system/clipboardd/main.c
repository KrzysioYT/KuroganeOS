#include "../../runtime/user.h"

#include <kurogane/clipboard.h>

#define CLIPBOARDD_MAX_CLIENTS 8U

typedef struct clipboardd_client {
    ku_service_connection_t connection;
    uint64_t pid;
    int active;
    int response_pending;
    ku_clipboard_response response;
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

static int utf8_valid(const uint8_t* data, size_t size) {
    size_t index = 0U;
    if (size == 0U || data[size - 1U] != 0U) return 0;
    --size;
    while (index < size) {
        const uint8_t lead = data[index++];
        uint32_t scalar;
        uint32_t minimum;
        size_t remaining;
        if (lead == 0U) return 0;
        if (lead < 0x80U) continue;
        if (lead >= 0xC2U && lead <= 0xDFU) {
            scalar = lead & 0x1FU; minimum = 0x80U; remaining = 1U;
        } else if (lead >= 0xE0U && lead <= 0xEFU) {
            scalar = lead & 0x0FU; minimum = 0x800U; remaining = 2U;
        } else if (lead >= 0xF0U && lead <= 0xF4U) {
            scalar = lead & 0x07U; minimum = 0x10000U; remaining = 3U;
        } else return 0;
        if (remaining > size - index) return 0;
        while (remaining-- != 0U) {
            const uint8_t next = data[index++];
            if ((next & 0xC0U) != 0x80U) return 0;
            scalar = (scalar << 6U) | (next & 0x3FU);
        }
        if (scalar < minimum || scalar > 0x10FFFFU ||
            (scalar >= 0xD800U && scalar <= 0xDFFFU)) return 0;
    }
    return 1;
}

static int payload_valid(uint32_t format, uint32_t data_size, const uint8_t* data) {
    if (data == (const uint8_t*)0 || data_size == 0U ||
        data_size > KU_CLIPBOARD_DATA_CAPACITY) return 0;
    switch (format) {
        case KU_CLIPBOARD_FORMAT_UTF8:
            return utf8_valid(data, data_size);
        case KU_CLIPBOARD_FORMAT_BINARY:
            return 1;
        default:
            return 0;
    }
}

static void queue_response(clipboardd_client* client, ku_status_t status) {
    ku_clipboard_response* response = &client->response;
    clear_bytes(response, sizeof(*response));
    response->structure_size = sizeof(*response);
    response->status = status;
    response->format = clipboard.format;
    response->data_size = clipboard.data_size;
    response->generation = clipboard.generation;
    response->owner_pid = clipboard.owner_pid;
    if (clipboard.data_size != 0U)
        copy_bytes(response->data, clipboard.data, clipboard.data_size);
    client->response_pending = 1;
}

static void flush_response(clipboardd_client* client) {
    ku_status_t status;
    if (!client->response_pending) return;
    status = ku_service_send(client->connection, &client->response,
        sizeof(client->response));
    if (status == KU_STATUS_WOULD_BLOCK) return;
    if (status != KU_STATUS_OK) {
        (void)ku_service_close(client->connection);
        clear_bytes(client, sizeof(*client));
        return;
    }
    clear_bytes(&client->response, sizeof(client->response));
    client->response_pending = 0;
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
        queue_response(client, KU_STATUS_CORRUPT_DATA);
        return;
    }
    request = (const ku_clipboard_request*)(const void*)message->data;
    if (request->structure_size != sizeof(*request)) {
        queue_response(client, KU_STATUS_INVALID_ARGUMENT);
        return;
    }
    if (client->pid == 0U) client->pid = message->sender_pid;
    else if (client->pid != message->sender_pid) {
        queue_response(client, KU_STATUS_ACCESS_DENIED);
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
    queue_response(client, status);
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
        if (client->response_pending) {
            flush_response(client);
            continue;
        }
        status = ku_service_receive(client->connection, &message);
        if (status == KU_STATUS_WOULD_BLOCK) continue;
        if (status != KU_STATUS_OK) {
            (void)ku_service_close(client->connection);
            clear_bytes(client, sizeof(*client));
            continue;
        }
        handle_request(client, &message);
        flush_response(client);
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

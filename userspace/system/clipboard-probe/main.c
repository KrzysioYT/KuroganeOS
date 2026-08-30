#include "../../runtime/user.h"

#include <kurogane/clipboard.h>

#define PROBE_ATTEMPTS 300U
#define PROBE_TEXT "Kurogane clipboard"

static void clear_bytes(void* destination, size_t size) {
    uint8_t* bytes = (uint8_t*)destination;
    size_t index;
    for (index = 0U; index < size; ++index) bytes[index] = 0U;
}

static int bytes_equal(const void* left, const void* right, size_t size) {
    const uint8_t* a = (const uint8_t*)left;
    const uint8_t* b = (const uint8_t*)right;
    size_t index;
    for (index = 0U; index < size; ++index) {
        if (a[index] != b[index]) return 0;
    }
    return 1;
}

static ku_status_t transact(
    ku_service_connection_t connection,
    const ku_clipboard_request* request,
    ku_clipboard_response* response) {
    uint32_t attempt;
    ku_status_t status = ku_service_send(connection, request, sizeof(*request));
    if (status != KU_STATUS_OK) return status;
    for (attempt = 0U; attempt < PROBE_ATTEMPTS; ++attempt) {
        ku_service_message message;
        status = ku_service_receive(connection, &message);
        if (status == KU_STATUS_WOULD_BLOCK) {
            (void)ku_sleep(1U);
            continue;
        }
        if (status != KU_STATUS_OK) return status;
        if (message.data_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        *response = *(const ku_clipboard_response*)(const void*)message.data;
        if (response->structure_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        return (ku_status_t)response->status;
    }
    return KU_STATUS_TIMED_OUT;
}

static void fail(uint32_t code) {
    (void)u_puts("[TEST] clipboard_service_roundtrip: FAIL\n");
    ku_exit((int32_t)code);
}

__attribute__((noreturn)) void _start(void) {
    ku_result_t connected = KU_STATUS_NOT_FOUND;
    ku_service_connection_t connection;
    ku_clipboard_request request;
    ku_clipboard_response response;
    uint64_t generation;
    const uint64_t self = ku_getpid();
    uint32_t attempt;
    const char text[] = PROBE_TEXT;

    if (self == 0U) fail(1U);
    for (attempt = 0U; attempt < PROBE_ATTEMPTS; ++attempt) {
        connected = ku_clipboard_connect();
        if (connected > 0) break;
        if (connected != KU_STATUS_NOT_FOUND && connected != KU_STATUS_WOULD_BLOCK) break;
        (void)ku_sleep(1U);
    }
    if (connected <= 0) fail(2U);
    connection = (ku_service_connection_t)connected;

    clear_bytes(&request, sizeof(request));
    clear_bytes(&response, sizeof(response));
    request.structure_size = sizeof(request);
    request.operation = KU_CLIPBOARD_SET;
    request.format = KU_CLIPBOARD_FORMAT_UTF8;
    request.data_size = sizeof(text);
    {
        uint32_t index;
        for (index = 0U; index < sizeof(text); ++index) request.data[index] = (uint8_t)text[index];
    }
    if (transact(connection, &request, &response) != KU_STATUS_OK ||
        response.format != KU_CLIPBOARD_FORMAT_UTF8 ||
        response.data_size != sizeof(text) ||
        response.generation == 0U ||
        response.owner_pid != self) {
        fail(3U);
    }
    generation = response.generation;

    clear_bytes(&request, sizeof(request));
    clear_bytes(&response, sizeof(response));
    request.structure_size = sizeof(request);
    request.operation = KU_CLIPBOARD_GET;
    if (transact(connection, &request, &response) != KU_STATUS_OK ||
        response.format != KU_CLIPBOARD_FORMAT_UTF8 ||
        response.data_size != sizeof(text) ||
        response.generation != generation ||
        response.owner_pid != self ||
        !bytes_equal(response.data, text, sizeof(text))) {
        fail(4U);
    }

    clear_bytes(&request, sizeof(request));
    clear_bytes(&response, sizeof(response));
    request.structure_size = sizeof(request);
    request.operation = KU_CLIPBOARD_CLEAR;
    request.generation = generation;
    if (transact(connection, &request, &response) != KU_STATUS_OK ||
        response.format != KU_CLIPBOARD_FORMAT_NONE ||
        response.data_size != 0U || response.owner_pid != 0U ||
        response.generation == generation) {
        fail(5U);
    }
    generation = response.generation;

    clear_bytes(&request, sizeof(request));
    clear_bytes(&response, sizeof(response));
    request.structure_size = sizeof(request);
    request.operation = KU_CLIPBOARD_GET;
    if (transact(connection, &request, &response) != KU_STATUS_NOT_FOUND ||
        response.generation != generation) {
        fail(6U);
    }

    (void)ku_service_close(connection);
    (void)u_puts("[TEST] clipboard_service_roundtrip: PASS\n");
    ku_exit(0);
}

#include "../../runtime/user.h"

#include <kurogane/clipboard.h>

static void zero_bytes(void* destination, size_t size) {
    uint8_t* bytes = (uint8_t*)destination;
    size_t index;
    for (index = 0U; index < size; ++index) bytes[index] = 0U;
}

static ku_result_t connect_retry(void) {
    uint32_t attempt;
    for (attempt = 0U; attempt < 240U; ++attempt) {
        const ku_result_t result = ku_clipboard_connect();
        if (result > 0) return result;
        if (result != KU_STATUS_NOT_FOUND && result != KU_STATUS_WOULD_BLOCK)
            return result;
        (void)ku_sleep(1U);
    }
    return KU_STATUS_NOT_FOUND;
}

static ku_status_t exchange(
    ku_service_connection_t connection,
    const ku_clipboard_request* request,
    ku_clipboard_response* response) {
    uint32_t attempt;
    ku_status_t status = ku_service_send(connection, request, sizeof(*request));
    if (status != KU_STATUS_OK) return status;
    for (attempt = 0U; attempt < 160U; ++attempt) {
        ku_service_message message;
        status = ku_service_receive(connection, &message);
        if (status == KU_STATUS_WOULD_BLOCK) {
            (void)ku_sleep(1U);
            continue;
        }
        if (status != KU_STATUS_OK) return status;
        if (message.data_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        {
            size_t i;
            const uint8_t* source = message.data;
            uint8_t* destination = (uint8_t*)response;
            for (i = 0U; i < sizeof(*response); ++i) destination[i] = source[i];
        }
        return (ku_status_t)response->status;
    }
    return KU_STATUS_WOULD_BLOCK;
}

__attribute__((noreturn)) void _start(void) {
    static const uint8_t trigger[11] = {
        'C','R','A','S','H','_','O','N','C','E',0
    };
    static const uint8_t recovered[10] = {
        'R','e','c','o','v','e','r','e','d',0
    };
    ku_clipboard_request request;
    ku_clipboard_response response;
    ku_result_t connected = connect_retry();
    ku_service_connection_t connection;
    size_t i;

    if (connected <= 0) ku_exit(1);
    connection = (ku_service_connection_t)connected;
    zero_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_CLIPBOARD_SET;
    request.format = KU_CLIPBOARD_FORMAT_UTF8;
    request.data_size = sizeof(trigger);
    for (i = 0U; i < sizeof(trigger); ++i) request.data[i] = trigger[i];
    if (exchange(connection, &request, &response) != KU_STATUS_OK) ku_exit(2);
    (void)ku_service_close(connection);

    (void)ku_sleep(UINT64_C(40));
    connected = connect_retry();
    if (connected <= 0) ku_exit(3);
    connection = (ku_service_connection_t)connected;

    zero_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_CLIPBOARD_SET;
    request.format = KU_CLIPBOARD_FORMAT_UTF8;
    request.data_size = sizeof(recovered);
    for (i = 0U; i < sizeof(recovered); ++i) request.data[i] = recovered[i];
    if (exchange(connection, &request, &response) != KU_STATUS_OK) ku_exit(4);

    zero_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_CLIPBOARD_GET;
    if (exchange(connection, &request, &response) != KU_STATUS_OK ||
        response.format != KU_CLIPBOARD_FORMAT_UTF8 ||
        response.data_size != sizeof(recovered)) ku_exit(5);
    for (i = 0U; i < sizeof(recovered); ++i) {
        if (response.data[i] != recovered[i]) ku_exit(6);
    }

    (void)ku_service_close(connection);
    (void)u_puts("[TEST] service_restart_rebind: PASS\n");
    ku_exit(0);
}

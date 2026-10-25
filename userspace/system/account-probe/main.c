#include "../../runtime/user.h"

#include <kurogane/account.h>

static void clear_bytes(void* destination, size_t size) {
    uint8_t* bytes = (uint8_t*)destination;
    size_t index;
    for (index = 0U; index < size; ++index) bytes[index] = 0U;
}

static int copy_text(char* destination, size_t capacity, const char* source) {
    size_t index = 0U;
    if (destination == (char*)0 || source == (const char*)0 || capacity == 0U) return 0;
    while (index + 1U < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    if (source[index] != '\0') return 0;
    destination[index] = '\0';
    return 1;
}

static ku_status_t transact(
    ku_service_connection_t connection,
    const ku_account_request* request,
    ku_account_response* response) {
    uint32_t attempts;
    ku_status_t status = ku_service_send(connection, request, sizeof(*request));
    if (status != KU_STATUS_OK) return status;
    for (attempts = 0U; attempts < 200U; ++attempts) {
        ku_service_message message;
        status = ku_service_receive(connection, &message);
        if (status == KU_STATUS_WOULD_BLOCK) {
            (void)ku_sleep(1U);
            continue;
        }
        if (status != KU_STATUS_OK) return status;
        if (message.data_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        *response = *(const ku_account_response*)(const void*)message.data;
        if (response->structure_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        return (ku_status_t)response->status;
    }
    return KU_STATUS_WOULD_BLOCK;
}

__attribute__((noreturn)) void _start(void) {
    ku_result_t connected = KU_STATUS_NOT_FOUND;
    ku_service_connection_t connection;
    ku_account_request request;
    ku_account_response current;
    ku_account_response lookup;
    uint32_t attempts;

    for (attempts = 0U; attempts < 200U; ++attempts) {
        connected = ku_service_connect(KU_ACCOUNT_SERVICE_NAME, KU_ACCOUNT_SERVICE_NAME_SIZE);
        if (connected > 0) break;
        if (connected != KU_STATUS_NOT_FOUND && connected != KU_STATUS_WOULD_BLOCK) break;
        (void)ku_sleep(1U);
    }
    if (connected <= 0) {
        (void)u_puts("[TEST] account_service_roundtrip: FAIL\n");
        ku_exit(1);
    }
    connection = (ku_service_connection_t)connected;

    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_ACCOUNT_GET_CURRENT;
    clear_bytes(&current, sizeof(current));
    if (transact(connection, &request, &current) != KU_STATUS_OK ||
        current.account_id == 0U || current.username[0] == '\0' ||
        current.locale[0] == '\0' ||
        (current.flags & KU_ACCOUNT_FLAG_PROFILE_VALID) == 0U ||
        ((current.flags & KU_ACCOUNT_FLAG_LIVE) == 0U &&
         (current.flags & KU_ACCOUNT_FLAG_INSTALLED) == 0U)) {
        (void)u_puts("[TEST] account_service_roundtrip: FAIL\n");
        ku_exit(2);
    }

    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_ACCOUNT_LOOKUP;
    if (!copy_text(request.username, sizeof(request.username), current.username)) {
        (void)u_puts("[TEST] account_service_roundtrip: FAIL\n");
        ku_exit(3);
    }
    clear_bytes(&lookup, sizeof(lookup));
    if (transact(connection, &request, &lookup) != KU_STATUS_OK ||
        lookup.account_id != current.account_id) {
        (void)u_puts("[TEST] account_service_roundtrip: FAIL\n");
        ku_exit(4);
    }

    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_ACCOUNT_LOOKUP;
    (void)copy_text(request.username, sizeof(request.username), "no-such-user");
    clear_bytes(&lookup, sizeof(lookup));
    if (transact(connection, &request, &lookup) != KU_STATUS_NOT_FOUND) {
        (void)u_puts("[TEST] account_service_roundtrip: FAIL\n");
        ku_exit(5);
    }

    (void)ku_service_close(connection);
    (void)u_puts("[TEST] account_service_roundtrip: PASS\n");
    ku_exit(0);
}

#include "../../runtime/user.h"

#include <kurogane/session.h>

static void clear_bytes(void* destination, size_t size) {
    uint8_t* bytes = (uint8_t*)destination;
    size_t index;
    for (index = 0U; index < size; ++index) bytes[index] = 0U;
}

static ku_status_t transact(
    ku_service_connection_t connection,
    const ku_session_request* request,
    ku_session_response* response) {
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
        *response = *(const ku_session_response*)(const void*)message.data;
        if (response->structure_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        return (ku_status_t)response->status;
    }
    return KU_STATUS_WOULD_BLOCK;
}

__attribute__((noreturn)) void _start(void) {
    ku_result_t connected = KU_STATUS_NOT_FOUND;
    ku_service_connection_t connection;
    ku_session_request request;
    ku_session_response response;
    ku_session_id_t session_id;
    const uint64_t self = ku_getpid();
    uint32_t attempts;

    for (attempts = 0U; attempts < 200U; ++attempts) {
        connected = ku_service_connect(KU_SESSION_SERVICE_NAME, KU_SESSION_SERVICE_NAME_SIZE);
        if (connected > 0) break;
        if (connected != KU_STATUS_NOT_FOUND && connected != KU_STATUS_WOULD_BLOCK) break;
        (void)ku_sleep(1U);
    }
    if (connected <= 0 || self == 0U) {
        (void)u_puts("[TEST] session_service_roundtrip: FAIL\n");
        ku_exit(1);
    }
    connection = (ku_service_connection_t)connected;

    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_SESSION_CREATE;
    request.account_id = UINT64_C(1);
    clear_bytes(&response, sizeof(response));
    if (transact(connection, &request, &response) != KU_STATUS_OK ||
        response.session_id == KU_SESSION_INVALID_ID || response.account_id != UINT64_C(1) ||
        response.owner_pid != self || response.state != KU_SESSION_STATE_ACTIVE) {
        (void)u_puts("[TEST] session_service_roundtrip: FAIL\n");
        ku_exit(2);
    }
    session_id = response.session_id;

    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_SESSION_SET_HOME;
    request.session_id = session_id;
    request.process_id = self;
    if (transact(connection, &request, &response) != KU_STATUS_OK || response.home_pid != self) {
        (void)u_puts("[TEST] session_service_roundtrip: FAIL\n");
        ku_exit(3);
    }

    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_SESSION_ATTACH_APPLICATION;
    request.session_id = session_id;
    request.process_id = self;
    if (transact(connection, &request, &response) != KU_STATUS_OK ||
        response.application_count != 1U || response.applications[0] != self) {
        (void)u_puts("[TEST] session_service_roundtrip: FAIL\n");
        ku_exit(4);
    }

    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_SESSION_QUERY;
    request.session_id = session_id;
    if (transact(connection, &request, &response) != KU_STATUS_OK ||
        response.home_pid != self || response.application_count != 1U) {
        (void)u_puts("[TEST] session_service_roundtrip: FAIL\n");
        ku_exit(5);
    }

    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_SESSION_DETACH_APPLICATION;
    request.session_id = session_id;
    request.process_id = self;
    if (transact(connection, &request, &response) != KU_STATUS_OK ||
        response.application_count != 0U || response.home_pid != 0U) {
        (void)u_puts("[TEST] session_service_roundtrip: FAIL\n");
        ku_exit(6);
    }

    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_SESSION_TERMINATE;
    request.session_id = session_id;
    if (transact(connection, &request, &response) != KU_STATUS_OK ||
        response.state != KU_SESSION_STATE_TERMINATING) {
        (void)u_puts("[TEST] session_service_roundtrip: FAIL\n");
        ku_exit(7);
    }

    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_SESSION_QUERY;
    request.session_id = session_id;
    if (transact(connection, &request, &response) != KU_STATUS_NOT_FOUND) {
        (void)u_puts("[TEST] session_service_roundtrip: FAIL\n");
        ku_exit(8);
    }

    (void)ku_service_close(connection);
    (void)u_puts("[TEST] session_service_roundtrip: PASS\n");
    ku_exit(0);
}

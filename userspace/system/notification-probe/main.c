#include "../../runtime/user.h"

#include <kurogane/notification.h>

static void clear_bytes(void* memory, size_t size) {
    size_t index = 0U;
    uint8_t* bytes = (uint8_t*)memory;
    for (; index < size; ++index) bytes[index] = 0U;
}

static void copy_text(char* destination, size_t capacity, const char* source) {
    size_t index = 0U;
    while (index + 1U < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index++] = '\0';
    while (index < capacity) destination[index++] = '\0';
}

static int text_equal(const char* left, const char* right, size_t capacity) {
    size_t index = 0U;
    for (; index < capacity; ++index) {
        if (left[index] != right[index]) return 0;
        if (left[index] == '\0') return 1;
    }
    return 0;
}

static ku_status_t transact(
    ku_service_connection_t connection,
    const ku_notification_request* request,
    ku_notification_response* response) {
    uint32_t attempts = 0U;
    ku_status_t status = ku_service_send(connection, request, sizeof(*request));
    if (status != KU_STATUS_OK) return status;
    while (attempts++ < 500U) {
        ku_service_message message;
        status = ku_service_receive(connection, &message);
        if (status == KU_STATUS_WOULD_BLOCK) {
            (void)ku_sleep(1U);
            continue;
        }
        if (status != KU_STATUS_OK) return status;
        if (message.data_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        *response = *(const ku_notification_response*)(const void*)message.data;
        if (response->structure_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        return response->status;
    }
    return KU_STATUS_TIMED_OUT;
}

static ku_result_t connect_notifications(void) {
    uint32_t attempts = 0U;
    while (attempts++ < 500U) {
        const ku_result_t result = ku_notification_connect();
        if (result > 0) return result;
        if (result != KU_STATUS_NOT_FOUND && result != KU_STATUS_WOULD_BLOCK)
            return result;
        (void)ku_sleep(1U);
    }
    return KU_STATUS_TIMED_OUT;
}

static ku_status_t post(
    ku_service_connection_t connection,
    ku_notification_response* response) {
    ku_notification_request request;
    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_NOTIFICATION_POST;
    request.type = KU_NOTIFICATION_TYPE_APPLICATION;
    request.priority = KU_NOTIFICATION_PRIORITY_HIGH;
    request.flags = KU_NOTIFICATION_FLAG_PUBLIC;
    copy_text(request.title, sizeof(request.title), "Road to 15");
    copy_text(request.body, sizeof(request.body), "Notification service roundtrip");
    return transact(connection, &request, response);
}

static ku_status_t list_public(
    ku_service_connection_t connection,
    uint64_t cursor,
    ku_notification_response* response) {
    ku_notification_request request;
    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_NOTIFICATION_LIST_PUBLIC;
    request.notification_id = cursor;
    return transact(connection, &request, response);
}

static ku_status_t by_id(
    ku_service_connection_t connection,
    uint32_t operation,
    uint64_t id,
    ku_notification_response* response) {
    ku_notification_request request;
    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = operation;
    request.notification_id = id;
    return transact(connection, &request, response);
}

__attribute__((noreturn)) void _start(void) {
    const ku_result_t connected = connect_notifications();
    ku_notification_response response;
    uint64_t id;

    if (connected <= 0) {
        (void)u_puts("[TEST] notification_service_roundtrip: FAIL\n");
        ku_exit(1);
    }

    if (post((ku_service_connection_t)connected, &response) != KU_STATUS_OK ||
        response.notification_id == 0U ||
        response.owner_pid != ku_getpid() ||
        response.type != KU_NOTIFICATION_TYPE_APPLICATION ||
        response.priority != KU_NOTIFICATION_PRIORITY_HIGH ||
        response.state != KU_NOTIFICATION_STATE_ACTIVE ||
        !text_equal(response.title, "Road to 15", sizeof(response.title)) ||
        !text_equal(response.body, "Notification service roundtrip", sizeof(response.body))) {
        (void)u_puts("[TEST] notification_service_post: FAIL\n");
        ku_exit(2);
    }
    id = response.notification_id;
    (void)u_puts("[TEST] notification_service_post: PASS\n");

    {
        uint64_t cursor = 0U;
        uint32_t attempts = 0U;
        int found = 0;
        while (attempts++ < 32U) {
            const ku_status_t listed = list_public(
                (ku_service_connection_t)connected, cursor, &response);
            if (listed == KU_STATUS_NOT_FOUND) break;
            if (listed != KU_STATUS_OK ||
                (response.flags & KU_NOTIFICATION_FLAG_PUBLIC) == 0U ||
                response.notification_id <= cursor) {
                (void)u_puts("[TEST] notification_service_public_list: FAIL\n");
                ku_exit(6);
            }
            cursor = response.notification_id;
            if (cursor == id) { found = 1; break; }
        }
        if (!found) {
            (void)u_puts("[TEST] notification_service_public_list: FAIL\n");
            ku_exit(6);
        }
        (void)u_puts("[TEST] notification_service_public_list: PASS\n");
    }

    if (by_id(
            (ku_service_connection_t)connected,
            KU_NOTIFICATION_GET,
            id,
            &response) != KU_STATUS_OK ||
        response.notification_id != id ||
        response.owner_pid != ku_getpid() ||
        response.state != KU_NOTIFICATION_STATE_ACTIVE ||
        !text_equal(response.title, "Road to 15", sizeof(response.title))) {
        (void)u_puts("[TEST] notification_service_get: FAIL\n");
        ku_exit(3);
    }
    (void)u_puts("[TEST] notification_service_get: PASS\n");

    if (by_id(
            (ku_service_connection_t)connected,
            KU_NOTIFICATION_DISMISS,
            id,
            &response) != KU_STATUS_OK ||
        response.notification_id != id ||
        response.state != KU_NOTIFICATION_STATE_DISMISSED) {
        (void)u_puts("[TEST] notification_service_dismiss: FAIL\n");
        ku_exit(4);
    }
    (void)u_puts("[TEST] notification_service_dismiss: PASS\n");

    if (by_id(
            (ku_service_connection_t)connected,
            KU_NOTIFICATION_GET,
            id,
            &response) != KU_STATUS_NOT_FOUND) {
        (void)u_puts("[TEST] notification_service_lifecycle: FAIL\n");
        ku_exit(5);
    }

    (void)u_puts("[TEST] notification_service_lifecycle: PASS\n");
    (void)u_puts("[TEST] notification_service_roundtrip: PASS\n");
    (void)ku_service_close((ku_service_connection_t)connected);
    ku_exit(0);
}

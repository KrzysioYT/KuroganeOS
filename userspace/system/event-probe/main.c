#include "../../runtime/user.h"

#include <kurogane/event_broker.h>

#define PROBE_TOPIC "qualification.event"

static void fill_topic(char* destination, const char* source) {
    size_t index = 0U;
    while (index < KU_EVENT_BROKER_TOPIC_CAPACITY) {
        destination[index] = source[index];
        if (source[index] == '\0') {
            ++index;
            break;
        }
        ++index;
    }
    while (index < KU_EVENT_BROKER_TOPIC_CAPACITY) destination[index++] = '\0';
}

static ku_status_t wait_response(
    ku_service_connection_t connection,
    ku_event_broker_response* response) {
    uint32_t attempts = 0U;
    while (attempts++ < 500U) {
        ku_service_message message;
        const ku_status_t status = ku_service_receive(connection, &message);
        if (status == KU_STATUS_WOULD_BLOCK) {
            (void)ku_sleep(1U);
            continue;
        }
        if (status != KU_STATUS_OK) return status;
        if (message.data_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        *response = *(const ku_event_broker_response*)(const void*)message.data;
        if (response->structure_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        return KU_STATUS_OK;
    }
    return KU_STATUS_TIMED_OUT;
}

static ku_status_t request(
    ku_service_connection_t connection,
    uint32_t operation,
    ku_event_broker_response* response) {
    ku_event_broker_request message;
    ku_status_t status;
    message.structure_size = sizeof(message);
    message.operation = operation;
    fill_topic(message.topic, PROBE_TOPIC);
    status = ku_service_send(connection, &message, sizeof(message));
    if (status != KU_STATUS_OK) return status;
    return wait_response(connection, response);
}

__attribute__((noreturn)) void _start(void) {
    ku_result_t connected = KU_STATUS_NOT_FOUND;
    uint32_t attempts = 0U;
    while (attempts++ < 500U) {
        connected = ku_event_broker_connect();
        if (connected > 0) break;
        if (connected != KU_STATUS_NOT_FOUND && connected != KU_STATUS_WOULD_BLOCK) break;
        (void)ku_sleep(1U);
    }
    if (connected <= 0) {
        (void)u_puts("[TEST] event_broker_roundtrip: FAIL\n");
        ku_exit(1);
    }

    const ku_service_connection_t connection = (ku_service_connection_t)connected;
    ku_event_broker_response response;
    ku_status_t status = request(connection, KU_EVENT_BROKER_SUBSCRIBE, &response);
    if (status != KU_STATUS_OK || response.status != KU_STATUS_OK || response.value == 0U) {
        (void)u_puts("[TEST] event_broker_roundtrip: FAIL\n");
        (void)ku_service_close(connection);
        ku_exit(2);
    }
    const ku_event_handle_t event = (ku_event_handle_t)response.value;

    status = request(connection, KU_EVENT_BROKER_PUBLISH, &response);
    if (status != KU_STATUS_OK || response.status != KU_STATUS_OK || response.value != 1U) {
        (void)u_puts("[TEST] event_broker_roundtrip: FAIL\n");
        (void)ku_event_close(event);
        (void)ku_service_close(connection);
        ku_exit(3);
    }

    status = ku_event_wait(event);
    if (status != KU_STATUS_OK) {
        (void)u_puts("[TEST] event_broker_roundtrip: FAIL\n");
        (void)ku_event_close(event);
        (void)ku_service_close(connection);
        ku_exit(4);
    }

    status = request(connection, KU_EVENT_BROKER_UNSUBSCRIBE, &response);
    if (status != KU_STATUS_OK || response.status != KU_STATUS_OK) {
        (void)u_puts("[TEST] event_broker_roundtrip: FAIL\n");
        (void)ku_event_close(event);
        (void)ku_service_close(connection);
        ku_exit(5);
    }

    (void)ku_event_close(event);
    (void)ku_service_close(connection);
    (void)u_puts("[TEST] event_broker_roundtrip: PASS\n");
    ku_exit(0);
}

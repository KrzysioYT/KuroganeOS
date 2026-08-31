#include "../../runtime/user.h"

#include <kurogane/event_broker.h>
#include <kurogane/network.h>
#include <kurogane/network_events.h>

#define NETWORK_EVENT_PROBE_ATTEMPTS 2000U

static void clear_bytes(void* destination, size_t size) {
    uint8_t* bytes = (uint8_t*)destination;
    size_t index;
    for (index = 0U; index < size; ++index) bytes[index] = 0U;
}

static void copy_topic(char destination[KU_EVENT_BROKER_TOPIC_CAPACITY], const char* source) {
    size_t index = 0U;
    while (index + 1U < KU_EVENT_BROKER_TOPIC_CAPACITY && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index++] = '\0';
    while (index < KU_EVENT_BROKER_TOPIC_CAPACITY) destination[index++] = '\0';
}

static void fail(uint32_t code) {
    (void)u_puts("[TEST] network_events: FAIL code=");
    (void)u_put_u64(code);
    (void)u_puts("\n");
    ku_exit((int32_t)code);
}

static ku_result_t connect_broker(void) {
    uint32_t attempt;
    ku_result_t result = KU_STATUS_NOT_FOUND;
    for (attempt = 0U; attempt < NETWORK_EVENT_PROBE_ATTEMPTS; ++attempt) {
        result = ku_event_broker_connect();
        if (result > 0) return result;
        if (result != KU_STATUS_NOT_FOUND && result != KU_STATUS_WOULD_BLOCK) return result;
        (void)ku_sleep(1U);
    }
    return result;
}

static ku_status_t wait_response(
    ku_service_connection_t connection,
    ku_event_broker_response* response) {
    uint32_t attempt;
    if (response == (ku_event_broker_response*)0) return KU_STATUS_INVALID_ARGUMENT;
    for (attempt = 0U; attempt < NETWORK_EVENT_PROBE_ATTEMPTS; ++attempt) {
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

static ku_result_t broker_operation(
    ku_service_connection_t connection,
    uint32_t operation,
    const char* topic) {
    ku_event_broker_request request;
    ku_event_broker_response response;
    ku_status_t status;
    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = operation;
    copy_topic(request.topic, topic);
    status = ku_service_send(connection, &request, sizeof(request));
    if (status != KU_STATUS_OK) return status;
    clear_bytes(&response, sizeof(response));
    status = wait_response(connection, &response);
    if (status != KU_STATUS_OK) return status;
    if (response.status != KU_STATUS_OK) return response.status;
    return (ku_result_t)response.value;
}

static ku_status_t wait_event_bounded(ku_event_handle_t event) {
    uint32_t attempt;
    for (attempt = 0U; attempt < NETWORK_EVENT_PROBE_ATTEMPTS; ++attempt) {
        const ku_status_t status = ku_event_poll(event);
        if (status == KU_STATUS_OK) return KU_STATUS_OK;
        if (status != KU_STATUS_WOULD_BLOCK) return status;
        (void)ku_sleep(1U);
    }
    return KU_STATUS_TIMED_OUT;
}

static int wait_link_state(int link_up) {
    uint32_t attempt;
    for (attempt = 0U; attempt < NETWORK_EVENT_PROBE_ATTEMPTS; ++attempt) {
        ku_network_status status;
        clear_bytes(&status, sizeof(status));
        status.structure_size = sizeof(status);
        if (ku_network_get_status(&status) == KU_STATUS_OK) {
            if (link_up) {
                if (status.ready != 0U && status.physical != 0U) return 1;
            } else if (status.ready == 0U && status.physical == 0U) {
                return 1;
            }
        }
        (void)ku_sleep(1U);
    }
    return 0;
}

static void unsubscribe(
    ku_service_connection_t connection,
    const char* topic) {
    (void)broker_operation(connection, KU_EVENT_BROKER_UNSUBSCRIBE, topic);
}

__attribute__((noreturn)) void _start(void) {
    const ku_result_t connected = connect_broker();
    ku_service_connection_t connection;
    ku_result_t result;
    ku_event_handle_t link_event;
    ku_event_handle_t changed_event;

    if (connected <= 0) fail(1U);
    connection = (ku_service_connection_t)connected;

    result = broker_operation(
        connection, KU_EVENT_BROKER_SUBSCRIBE, KU_NETWORK_EVENT_LINK);
    if (result <= 0) {
        (void)ku_service_close(connection);
        fail(2U);
    }
    link_event = (ku_event_handle_t)result;

    result = broker_operation(
        connection, KU_EVENT_BROKER_SUBSCRIBE, KU_NETWORK_EVENT_CHANGED);
    if (result <= 0) {
        (void)ku_event_close(link_event);
        (void)ku_service_close(connection);
        fail(3U);
    }
    changed_event = (ku_event_handle_t)result;

    if (!wait_link_state(1)) {
        (void)ku_event_close(changed_event);
        (void)ku_event_close(link_event);
        (void)ku_service_close(connection);
        fail(4U);
    }
    (void)u_puts("[TEST] network_events_armed: PASS\n");

    if (wait_event_bounded(link_event) != KU_STATUS_OK) fail(5U);
    if (wait_event_bounded(changed_event) != KU_STATUS_OK) fail(6U);
    if (!wait_link_state(0)) fail(7U);
    (void)u_puts("[TEST] network_events_down: PASS\n");

    if (wait_event_bounded(link_event) != KU_STATUS_OK) fail(8U);
    if (wait_event_bounded(changed_event) != KU_STATUS_OK) fail(9U);
    if (!wait_link_state(1)) fail(10U);

    unsubscribe(connection, KU_NETWORK_EVENT_CHANGED);
    unsubscribe(connection, KU_NETWORK_EVENT_LINK);
    (void)ku_event_close(changed_event);
    (void)ku_event_close(link_event);
    (void)ku_service_close(connection);
    (void)u_puts("[TEST] network_events: PASS\n");
    ku_exit(0);
}

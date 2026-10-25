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

static void fail_stage(const char* stage) {
    (void)u_puts("[TEST] event_broker_stage: FAIL ");
    (void)u_puts(stage);
    (void)u_puts("\n");
    (void)u_puts("[TEST] event_broker_roundtrip: FAIL\n");
}

static ku_status_t send_request(
    ku_service_connection_t connection,
    uint32_t operation) {
    ku_event_broker_request message;
    message.structure_size = sizeof(message);
    message.operation = operation;
    fill_topic(message.topic, PROBE_TOPIC);
    return ku_service_send(connection, &message, sizeof(message));
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

static void fail_receive_stage(const char* prefix, ku_status_t status) {
    if (status == KU_STATUS_TIMED_OUT) {
        if (prefix[0] == 's') fail_stage("subscribe-timeout");
        else if (prefix[0] == 'p') fail_stage("publish-timeout");
        else fail_stage("unsubscribe-timeout");
        return;
    }
    if (status == KU_STATUS_CORRUPT_DATA) {
        if (prefix[0] == 's') fail_stage("subscribe-response-corrupt");
        else if (prefix[0] == 'p') fail_stage("publish-response-corrupt");
        else fail_stage("unsubscribe-response-corrupt");
        return;
    }
    if (prefix[0] == 's') fail_stage("subscribe-receive-error");
    else if (prefix[0] == 'p') fail_stage("publish-receive-error");
    else fail_stage("unsubscribe-receive-error");
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
        fail_stage("connect");
        ku_exit(1);
    }

    const ku_service_connection_t connection = (ku_service_connection_t)connected;
    ku_event_broker_response response;
    ku_status_t status = send_request(connection, KU_EVENT_BROKER_SUBSCRIBE);
    if (status != KU_STATUS_OK) {
        fail_stage("subscribe-send");
        (void)ku_service_close(connection);
        ku_exit(2);
    }
    status = wait_response(connection, &response);
    if (status != KU_STATUS_OK) {
        fail_receive_stage("subscribe", status);
        (void)ku_service_close(connection);
        ku_exit(3);
    }
    if (response.status != KU_STATUS_OK) {
        fail_stage("subscribe-status");
        (void)ku_service_close(connection);
        ku_exit(4);
    }
    if (response.value == 0U) {
        fail_stage("subscribe-handle");
        (void)ku_service_close(connection);
        ku_exit(5);
    }
    const ku_event_handle_t event = (ku_event_handle_t)response.value;

    status = send_request(connection, KU_EVENT_BROKER_PUBLISH);
    if (status != KU_STATUS_OK) {
        fail_stage("publish-send");
        (void)ku_event_close(event);
        (void)ku_service_close(connection);
        ku_exit(6);
    }
    status = wait_response(connection, &response);
    if (status != KU_STATUS_OK) {
        fail_receive_stage("publish", status);
        (void)ku_event_close(event);
        (void)ku_service_close(connection);
        ku_exit(7);
    }
    if (response.status != KU_STATUS_OK) {
        fail_stage("publish-status");
        (void)ku_event_close(event);
        (void)ku_service_close(connection);
        ku_exit(8);
    }
    if (response.value != 1U) {
        fail_stage("publish-count");
        (void)ku_event_close(event);
        (void)ku_service_close(connection);
        ku_exit(9);
    }

    status = ku_event_wait(event);
    if (status != KU_STATUS_OK) {
        fail_stage("wait-event");
        (void)ku_event_close(event);
        (void)ku_service_close(connection);
        ku_exit(10);
    }

    status = send_request(connection, KU_EVENT_BROKER_UNSUBSCRIBE);
    if (status != KU_STATUS_OK) {
        fail_stage("unsubscribe-send");
        (void)ku_event_close(event);
        (void)ku_service_close(connection);
        ku_exit(11);
    }
    status = wait_response(connection, &response);
    if (status != KU_STATUS_OK) {
        fail_receive_stage("unsubscribe", status);
        (void)ku_event_close(event);
        (void)ku_service_close(connection);
        ku_exit(12);
    }
    if (response.status != KU_STATUS_OK) {
        fail_stage("unsubscribe-status");
        (void)ku_event_close(event);
        (void)ku_service_close(connection);
        ku_exit(13);
    }

    (void)ku_event_close(event);
    (void)ku_service_close(connection);
    (void)u_puts("[TEST] event_broker_roundtrip: PASS\n");
    ku_exit(0);
}
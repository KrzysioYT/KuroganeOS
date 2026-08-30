#include "../../runtime/user.h"

#include <kurogane/event_broker.h>
#include <kurogane/network.h>
#include <kurogane/network_events.h>

#define BROKER_RETRY_LIMIT 256U
#define BROKER_RESPONSE_LIMIT 128U
#define NETWORK_POLL_INTERVAL UINT64_C(10)

static void topic_copy(char* destination, const char* source) {
    size_t index = 0U;
    while (index < KU_EVENT_BROKER_TOPIC_CAPACITY) {
        const char value = source[index];
        destination[index] = value;
        ++index;
        if (value == '\0') break;
    }
    while (index < KU_EVENT_BROKER_TOPIC_CAPACITY) destination[index++] = '\0';
}

static int ipv4_equal(const uint8_t left[4], const uint8_t right[4]) {
    size_t index = 0U;
    for (; index < 4U; ++index) {
        if (left[index] != right[index]) return 0;
    }
    return 1;
}

static ku_service_connection_t connect_broker(void) {
    uint32_t attempt = 0U;
    for (; attempt < BROKER_RETRY_LIMIT; ++attempt) {
        const ku_result_t result = ku_event_broker_connect();
        if (result > 0) return (ku_service_connection_t)result;
        if (result != KU_STATUS_NOT_FOUND && result != KU_STATUS_WOULD_BLOCK) {
            return 0U;
        }
        (void)ku_sleep(1U);
    }
    return 0U;
}

static ku_status_t wait_response(
    ku_service_connection_t connection,
    ku_event_broker_response* response) {
    uint32_t attempt = 0U;
    if (response == (ku_event_broker_response*)0) return KU_STATUS_INVALID_ARGUMENT;
    for (; attempt < BROKER_RESPONSE_LIMIT; ++attempt) {
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

static ku_status_t publish_topic(
    ku_service_connection_t connection,
    const char* topic) {
    ku_event_broker_request request;
    ku_event_broker_response response;
    ku_status_t status;
    if (connection == 0U || topic == (const char*)0) return KU_STATUS_INVALID_ARGUMENT;
    request.structure_size = sizeof(request);
    request.operation = KU_EVENT_BROKER_PUBLISH;
    topic_copy(request.topic, topic);
    status = ku_service_send(connection, &request, sizeof(request));
    if (status != KU_STATUS_OK) return status;
    status = wait_response(connection, &response);
    if (status != KU_STATUS_OK) return status;
    return response.status;
}

static ku_status_t publish_changes(
    ku_service_connection_t connection,
    const ku_network_status* before,
    const ku_network_status* after) {
    ku_status_t first_error = KU_STATUS_OK;
    ku_status_t status;
    int changed = 0;
    if (before == (const ku_network_status*)0 ||
        after == (const ku_network_status*)0) {
        return KU_STATUS_INVALID_ARGUMENT;
    }

    if (before->ready != after->ready || before->dhcp != after->dhcp) {
        status = publish_topic(connection, KU_NETWORK_EVENT_READY);
        if (status != KU_STATUS_OK && first_error == KU_STATUS_OK) first_error = status;
        changed = 1;
    }
    if (before->physical != after->physical) {
        status = publish_topic(connection, KU_NETWORK_EVENT_LINK);
        if (status != KU_STATUS_OK && first_error == KU_STATUS_OK) first_error = status;
        changed = 1;
    }
    if (!ipv4_equal(before->address, after->address) ||
        !ipv4_equal(before->gateway, after->gateway)) {
        status = publish_topic(connection, KU_NETWORK_EVENT_ADDRESS);
        if (status != KU_STATUS_OK && first_error == KU_STATUS_OK) first_error = status;
        changed = 1;
    }
    if (!ipv4_equal(before->dns, after->dns)) {
        status = publish_topic(connection, KU_NETWORK_EVENT_DNS);
        if (status != KU_STATUS_OK && first_error == KU_STATUS_OK) first_error = status;
        changed = 1;
    }
    if (changed) {
        status = publish_topic(connection, KU_NETWORK_EVENT_CHANGED);
        if (status != KU_STATUS_OK && first_error == KU_STATUS_OK) first_error = status;
    }
    return first_error;
}

static void clear_status(ku_network_status* status) {
    size_t index = 0U;
    if (status == (ku_network_status*)0) return;
    status->structure_size = sizeof(*status);
    status->ready = 0U;
    status->physical = 0U;
    status->dhcp = 0U;
    for (; index < 4U; ++index) {
        status->address[index] = 0U;
        status->gateway[index] = 0U;
        status->dns[index] = 0U;
        status->reserved0[index] = 0U;
    }
    status->bytes_received = 0U;
    status->bytes_transmitted = 0U;
}

__attribute__((noreturn)) void _start(void) {
    ku_service_connection_t broker = 0U;
    ku_network_status previous;
    int have_previous = 0;

    clear_status(&previous);
    for (;;) {
        ku_network_status current;
        const ku_status_t status;

        if (broker == 0U) broker = connect_broker();
        clear_status(&current);
        status = ku_network_get_status(&current);
        if (status == KU_STATUS_OK) {
            if (have_previous && broker != 0U) {
                const ku_status_t publish_status =
                    publish_changes(broker, &previous, &current);
                if (publish_status != KU_STATUS_OK) {
                    (void)ku_service_close(broker);
                    broker = 0U;
                }
            }
            previous = current;
            have_previous = 1;
        }

        (void)ku_sleep(NETWORK_POLL_INTERVAL);
        (void)ku_yield();
    }
}

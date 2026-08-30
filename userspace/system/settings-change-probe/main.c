#include "../../runtime/user.h"

#include <kurogane/event_broker.h>
#include <kurogane/settings.h>

#define CHANGE_KEY "qualification.change"
#define PROBE_ATTEMPTS 400U

static void clear_bytes(void* destination, size_t size) {
    uint8_t* bytes = (uint8_t*)destination;
    size_t index;
    for (index = 0U; index < size; ++index) bytes[index] = 0U;
}

static void copy_text(char* destination, size_t capacity, const char* source) {
    size_t index = 0U;
    while (index + 1U < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

static ku_result_t connect_retry(const char* name, size_t name_size) {
    uint32_t attempt;
    ku_result_t result = KU_STATUS_NOT_FOUND;
    for (attempt = 0U; attempt < PROBE_ATTEMPTS; ++attempt) {
        result = ku_service_connect(name, name_size);
        if (result > 0) return result;
        if (result != KU_STATUS_NOT_FOUND && result != KU_STATUS_WOULD_BLOCK) return result;
        (void)ku_sleep(1U);
    }
    return result;
}

static ku_status_t wait_broker_response(
    ku_service_connection_t connection,
    ku_event_broker_response* response) {
    uint32_t attempt;
    for (attempt = 0U; attempt < PROBE_ATTEMPTS; ++attempt) {
        ku_service_message message;
        ku_status_t status = ku_service_receive(connection, &message);
        if (status == KU_STATUS_WOULD_BLOCK) {
            (void)ku_sleep(1U);
            continue;
        }
        if (status != KU_STATUS_OK) return status;
        if (message.data_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        *response = *(const ku_event_broker_response*)(const void*)message.data;
        if (response->structure_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        return (ku_status_t)response->status;
    }
    return KU_STATUS_TIMED_OUT;
}

static ku_status_t broker_request(
    ku_service_connection_t connection,
    uint32_t operation,
    ku_event_broker_response* response) {
    ku_event_broker_request request;
    ku_status_t status;
    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = operation;
    copy_text(request.topic, sizeof(request.topic), KU_SETTINGS_CHANGED_TOPIC);
    status = ku_service_send(connection, &request, sizeof(request));
    if (status != KU_STATUS_OK) return status;
    return wait_broker_response(connection, response);
}

static ku_status_t settings_request(
    ku_service_connection_t connection,
    uint32_t operation,
    uint32_t type,
    const void* value,
    uint32_t value_size,
    ku_settings_response* response) {
    ku_settings_request request;
    uint32_t attempt;
    ku_status_t status;

    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = operation;
    request.type = type;
    request.value_size = value_size;
    copy_text(request.key, sizeof(request.key), CHANGE_KEY);
    if (value != (const void*)0 && value_size != 0U) {
        const uint8_t* input = (const uint8_t*)value;
        uint32_t index;
        if (value_size > sizeof(request.value)) return KU_STATUS_INVALID_ARGUMENT;
        for (index = 0U; index < value_size; ++index) request.value[index] = input[index];
    }
    status = ku_service_send(connection, &request, sizeof(request));
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
        *response = *(const ku_settings_response*)(const void*)message.data;
        if (response->structure_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        return (ku_status_t)response->status;
    }
    return KU_STATUS_TIMED_OUT;
}

static void fail(uint32_t code) {
    (void)u_puts("[TEST] settings_change_notification: FAIL\n");
    ku_exit((int32_t)code);
}

__attribute__((noreturn)) void _start(void) {
    ku_result_t broker_connected;
    ku_result_t settings_connected;
    ku_service_connection_t broker;
    ku_service_connection_t settings;
    ku_event_broker_response broker_response;
    ku_settings_response settings_response;
    ku_event_handle_t changed_event;
    uint64_t value = UINT64_C(0x4B55524F47414E45);

    broker_connected = connect_retry(
        KU_EVENT_BROKER_SERVICE_NAME,
        KU_EVENT_BROKER_SERVICE_NAME_SIZE);
    if (broker_connected <= 0) fail(1U);
    broker = (ku_service_connection_t)broker_connected;

    clear_bytes(&broker_response, sizeof(broker_response));
    if (broker_request(broker, KU_EVENT_BROKER_SUBSCRIBE, &broker_response) != KU_STATUS_OK ||
        broker_response.value == 0U) {
        (void)ku_service_close(broker);
        fail(2U);
    }
    changed_event = (ku_event_handle_t)broker_response.value;

    settings_connected = connect_retry(KU_SETTINGS_SERVICE_NAME, KU_SETTINGS_SERVICE_NAME_SIZE);
    if (settings_connected <= 0) {
        (void)ku_event_close(changed_event);
        (void)ku_service_close(broker);
        fail(3U);
    }
    settings = (ku_service_connection_t)settings_connected;

    clear_bytes(&settings_response, sizeof(settings_response));
    if (settings_request(
            settings,
            KU_SETTINGS_SET,
            KU_SETTINGS_TYPE_U64,
            &value,
            sizeof(value),
            &settings_response) != KU_STATUS_OK) {
        fail(4U);
    }
    if (ku_event_wait(changed_event) != KU_STATUS_OK) fail(5U);

    clear_bytes(&settings_response, sizeof(settings_response));
    if (settings_request(
            settings,
            KU_SETTINGS_GET,
            KU_SETTINGS_TYPE_NONE,
            (const void*)0,
            0U,
            &settings_response) != KU_STATUS_OK ||
        settings_response.type != KU_SETTINGS_TYPE_U64 ||
        settings_response.value_size != sizeof(value)) {
        fail(6U);
    }

    clear_bytes(&settings_response, sizeof(settings_response));
    if (settings_request(
            settings,
            KU_SETTINGS_DELETE,
            KU_SETTINGS_TYPE_NONE,
            (const void*)0,
            0U,
            &settings_response) != KU_STATUS_OK) {
        fail(7U);
    }
    if (ku_event_wait(changed_event) != KU_STATUS_OK) fail(8U);

    clear_bytes(&settings_response, sizeof(settings_response));
    if (settings_request(
            settings,
            KU_SETTINGS_GET,
            KU_SETTINGS_TYPE_NONE,
            (const void*)0,
            0U,
            &settings_response) != KU_STATUS_NOT_FOUND) {
        fail(9U);
    }

    clear_bytes(&broker_response, sizeof(broker_response));
    if (broker_request(broker, KU_EVENT_BROKER_UNSUBSCRIBE, &broker_response) != KU_STATUS_OK) {
        fail(10U);
    }
    (void)ku_event_close(changed_event);
    (void)ku_service_close(settings);
    (void)ku_service_close(broker);
    (void)u_puts("[TEST] settings_change_notification: PASS\n");
    ku_exit(0);
}

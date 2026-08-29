#include "../../runtime/user.h"

#include <kurogane/filesystem.h>
#include <kurogane/settings.h>

#define SETTINGS_DB_PATH "/settings.db"

static void clear_bytes(void* memory, size_t size) {
    size_t index;
    uint8_t* bytes = (uint8_t*)memory;
    for (index = 0U; index < size; ++index) bytes[index] = 0U;
}

static void copy_key(char* destination, const char* source) {
    size_t index = 0U;
    while (index < KU_SETTINGS_KEY_CAPACITY) {
        destination[index] = source[index];
        if (source[index] == '\0') {
            ++index;
            break;
        }
        ++index;
    }
    while (index < KU_SETTINGS_KEY_CAPACITY) destination[index++] = '\0';
}

static ku_status_t transact(
    ku_service_connection_t connection,
    const ku_settings_request* request,
    ku_settings_response* response) {
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
        *response = *(const ku_settings_response*)(const void*)message.data;
        if (response->structure_size != sizeof(*response) ||
            response->value_size > KU_SETTINGS_VALUE_CAPACITY) {
            return KU_STATUS_CORRUPT_DATA;
        }
        return response->status;
    }
    return KU_STATUS_TIMED_OUT;
}

static ku_status_t set_bool(
    ku_service_connection_t connection,
    const char* key,
    uint8_t value) {
    ku_settings_request request;
    ku_settings_response response;
    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_SETTINGS_SET;
    request.type = KU_SETTINGS_TYPE_BOOL;
    request.value_size = 1U;
    copy_key(request.key, key);
    request.value[0] = value;
    return transact(connection, &request, &response);
}

static ku_status_t set_u64(
    ku_service_connection_t connection,
    const char* key,
    uint64_t value) {
    ku_settings_request request;
    ku_settings_response response;
    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_SETTINGS_SET;
    request.type = KU_SETTINGS_TYPE_U64;
    request.value_size = sizeof(value);
    copy_key(request.key, key);
    *(uint64_t*)(void*)request.value = value;
    return transact(connection, &request, &response);
}

static ku_status_t set_string(
    ku_service_connection_t connection,
    const char* key,
    const char* value) {
    ku_settings_request request;
    ku_settings_response response;
    size_t size = u_strlen(value) + 1U;
    size_t index;
    if (size > KU_SETTINGS_VALUE_CAPACITY) return KU_STATUS_OUT_OF_RANGE;
    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_SETTINGS_SET;
    request.type = KU_SETTINGS_TYPE_STRING;
    request.value_size = (uint32_t)size;
    copy_key(request.key, key);
    for (index = 0U; index < size; ++index) request.value[index] = (uint8_t)value[index];
    return transact(connection, &request, &response);
}

static ku_status_t get_value(
    ku_service_connection_t connection,
    const char* key,
    ku_settings_response* response) {
    ku_settings_request request;
    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_SETTINGS_GET;
    request.type = KU_SETTINGS_TYPE_NONE;
    request.value_size = 0U;
    copy_key(request.key, key);
    return transact(connection, &request, response);
}

static int response_bool(const ku_settings_response* response, uint8_t expected) {
    return response->status == KU_STATUS_OK &&
        response->type == KU_SETTINGS_TYPE_BOOL &&
        response->value_size == 1U && response->value[0] == expected;
}

static int response_u64(const ku_settings_response* response, uint64_t expected) {
    if (response->status != KU_STATUS_OK || response->type != KU_SETTINGS_TYPE_U64 ||
        response->value_size != sizeof(uint64_t)) return 0;
    return *(const uint64_t*)(const void*)response->value == expected;
}

static int response_string(const ku_settings_response* response, const char* expected) {
    size_t expected_size = u_strlen(expected) + 1U;
    size_t index;
    if (response->status != KU_STATUS_OK || response->type != KU_SETTINGS_TYPE_STRING ||
        response->value_size != expected_size) return 0;
    for (index = 0U; index < expected_size; ++index) {
        if (response->value[index] != (uint8_t)expected[index]) return 0;
    }
    return 1;
}

static ku_result_t connect_settings(void) {
    uint32_t attempts = 0U;
    while (attempts++ < 500U) {
        ku_result_t result = ku_settings_connect();
        if (result > 0) return result;
        if (result != KU_STATUS_NOT_FOUND && result != KU_STATUS_WOULD_BLOCK) return result;
        (void)ku_sleep(1U);
    }
    return KU_STATUS_TIMED_OUT;
}

__attribute__((noreturn)) void _start(void) {
    ku_file_stat info;
    ku_status_t disk_status = ku_file_stat_path(
        SETTINGS_DB_PATH,
        sizeof(SETTINGS_DB_PATH) - 1U,
        &info);
    const int persisted_boot = disk_status == KU_STATUS_OK &&
        info.type == KU_FILE_TYPE_REGULAR && info.size > 16U;
    ku_result_t connected = connect_settings();
    ku_settings_response response;

    if (connected <= 0) {
        (void)u_puts("[TEST] settings_service_roundtrip: FAIL\n");
        ku_exit(1);
    }

    if (persisted_boot) {
        if (get_value((ku_service_connection_t)connected, "ui.dark", &response) != KU_STATUS_OK ||
            !response_bool(&response, 1U)) ku_exit(2);
        if (get_value((ku_service_connection_t)connected, "session.limit", &response) != KU_STATUS_OK ||
            !response_u64(&response, UINT64_C(42))) ku_exit(3);
        if (get_value((ku_service_connection_t)connected, "desktop.theme", &response) != KU_STATUS_OK ||
            !response_string(&response, "forge")) ku_exit(4);
        (void)u_puts("[TEST] settings_service_persist_reload: PASS\n");
        (void)u_puts("[TEST] settings_service_roundtrip: PASS\n");
        (void)ku_service_close((ku_service_connection_t)connected);
        ku_exit(0);
    }

    if (disk_status != KU_STATUS_NOT_FOUND) {
        (void)u_puts("[TEST] settings_service_persist_write: FAIL\n");
        ku_exit(5);
    }
    if (set_bool((ku_service_connection_t)connected, "ui.dark", 1U) != KU_STATUS_OK ||
        set_u64((ku_service_connection_t)connected, "session.limit", UINT64_C(42)) != KU_STATUS_OK ||
        set_string((ku_service_connection_t)connected, "desktop.theme", "forge") != KU_STATUS_OK) {
        (void)u_puts("[TEST] settings_service_persist_write: FAIL\n");
        ku_exit(6);
    }
    if (get_value((ku_service_connection_t)connected, "ui.dark", &response) != KU_STATUS_OK ||
        !response_bool(&response, 1U) ||
        get_value((ku_service_connection_t)connected, "session.limit", &response) != KU_STATUS_OK ||
        !response_u64(&response, UINT64_C(42)) ||
        get_value((ku_service_connection_t)connected, "desktop.theme", &response) != KU_STATUS_OK ||
        !response_string(&response, "forge")) {
        (void)u_puts("[TEST] settings_service_roundtrip: FAIL\n");
        ku_exit(7);
    }
    disk_status = ku_file_stat_path(SETTINGS_DB_PATH, sizeof(SETTINGS_DB_PATH) - 1U, &info);
    if (disk_status != KU_STATUS_OK || info.type != KU_FILE_TYPE_REGULAR || info.size <= 16U) {
        (void)u_puts("[TEST] settings_service_persist_write: FAIL\n");
        ku_exit(8);
    }
    (void)u_puts("[TEST] settings_service_persist_write: PASS\n");
    (void)u_puts("[TEST] settings_service_roundtrip: PASS\n");
    (void)ku_service_close((ku_service_connection_t)connected);
    ku_exit(0);
}

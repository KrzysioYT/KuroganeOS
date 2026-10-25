#include "../../runtime/user.h"

#include <kurogane/application.h>
#include <kurogane/filesystem.h>

#define APPREG_PROBE_ATTEMPTS 2000U
#define APPREG_EXPECTED_COUNT 7U

static void clear_bytes(void* destination, size_t size) {
    uint8_t* bytes = (uint8_t*)destination;
    size_t index;
    for (index = 0U; index < size; ++index) bytes[index] = 0U;
}

static size_t text_length(const char* text, size_t capacity) {
    size_t length = 0U;
    if (text == (const char*)0) return 0U;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

static int text_equal(const char* left, const char* right) {
    size_t index = 0U;
    if (left == (const char*)0 || right == (const char*)0) return 0;
    while (left[index] != '\0' && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

static void copy_id(char destination[KU_APPLICATION_ID_CAPACITY], const char* source) {
    size_t index = 0U;
    while (index + 1U < KU_APPLICATION_ID_CAPACITY && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    while (index < KU_APPLICATION_ID_CAPACITY) destination[index++] = '\0';
}

static void fail(uint32_t code) {
    (void)u_puts("[TEST] app_registry: FAIL code=");
    (void)u_put_u64(code);
    (void)u_puts("\n");
    ku_exit((int32_t)code);
}

static ku_result_t connect_registry(void) {
    uint32_t attempt;
    ku_result_t result = KU_STATUS_NOT_FOUND;
    for (attempt = 0U; attempt < APPREG_PROBE_ATTEMPTS; ++attempt) {
        result = ku_service_connect(
            KU_APPLICATION_SERVICE_NAME,
            KU_APPLICATION_SERVICE_NAME_SIZE);
        if (result > 0) return result;
        if (result != KU_STATUS_NOT_FOUND && result != KU_STATUS_WOULD_BLOCK) return result;
        (void)ku_sleep(1U);
    }
    return result;
}

static ku_status_t wait_response(
    ku_service_connection_t connection,
    ku_application_response* response) {
    uint32_t attempt;
    for (attempt = 0U; attempt < APPREG_PROBE_ATTEMPTS; ++attempt) {
        ku_service_message message;
        const ku_status_t status = ku_service_receive(connection, &message);
        if (status == KU_STATUS_WOULD_BLOCK) {
            (void)ku_sleep(1U);
            continue;
        }
        if (status != KU_STATUS_OK) return status;
        if (message.data_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        *response = *(const ku_application_response*)(const void*)message.data;
        if (response->structure_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        return KU_STATUS_OK;
    }
    return KU_STATUS_TIMED_OUT;
}

static ku_status_t transact(
    ku_service_connection_t connection,
    const ku_application_request* request,
    ku_application_response* response) {
    ku_status_t status = ku_service_send(connection, request, sizeof(*request));
    if (status != KU_STATUS_OK) return status;
    clear_bytes(response, sizeof(*response));
    return wait_response(connection, response);
}

static int executable_exists(const char* path) {
    const size_t length = text_length(path, KU_APPLICATION_EXECUTABLE_CAPACITY);
    const ku_result_t opened = ku_file_open(path, length);
    if (length == 0U || length >= KU_APPLICATION_EXECUTABLE_CAPACITY || opened <= 0) return 0;
    return ku_file_close((ku_file_t)opened) == KU_STATUS_OK;
}

static ku_status_t lookup(
    ku_service_connection_t connection,
    const char* id,
    ku_application_response* response) {
    ku_application_request request;
    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_APPLICATION_LOOKUP;
    copy_id(request.id, id);
    return transact(connection, &request, response);
}

__attribute__((noreturn)) void _start(void) {
    const ku_result_t connected = connect_registry();
    ku_service_connection_t connection;
    ku_application_request request;
    ku_application_response response;
    char ids[KU_APPLICATION_MAX_ENTRIES][KU_APPLICATION_ID_CAPACITY];
    uint32_t count;
    uint32_t index;
    uint32_t previous;
    uint32_t saw_probe = 0U;
    uint32_t saw_duplicate = 0U;
    uint32_t saw_about = 0U;

    if (connected <= 0) fail(1U);
    connection = (ku_service_connection_t)connected;
    clear_bytes(ids, sizeof(ids));

    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_APPLICATION_GET_COUNT;
    if (transact(connection, &request, &response) != KU_STATUS_OK ||
        response.status != KU_STATUS_OK) {
        fail(2U);
    }
    count = response.count;
    if (count != APPREG_EXPECTED_COUNT || count > KU_APPLICATION_MAX_ENTRIES) fail(3U);

    for (index = 0U; index < count; ++index) {
        size_t id_length;
        size_t name_length;
        size_t executable_length;
        clear_bytes(&request, sizeof(request));
        request.structure_size = sizeof(request);
        request.operation = KU_APPLICATION_GET_BY_INDEX;
        request.index = index;
        if (transact(connection, &request, &response) != KU_STATUS_OK ||
            response.status != KU_STATUS_OK || response.index != index ||
            response.count != count || response.manifest_version != 1U) {
            fail(4U);
        }
        id_length = text_length(response.id, sizeof(response.id));
        name_length = text_length(response.name, sizeof(response.name));
        executable_length = text_length(response.executable, sizeof(response.executable));
        if (id_length == 0U || id_length >= sizeof(response.id) ||
            name_length == 0U || name_length >= sizeof(response.name) ||
            executable_length == 0U || executable_length >= sizeof(response.executable) ||
            response.executable[0] != '/' || !executable_exists(response.executable)) {
            fail(5U);
        }
        for (previous = 0U; previous < index; ++previous) {
            if (text_equal(ids[previous], response.id)) fail(6U);
        }
        copy_id(ids[index], response.id);
        if (text_equal(response.id, "org.kurogane.probe")) ++saw_probe;
        if (text_equal(response.id, "org.kurogane.dupe")) ++saw_duplicate;
        if (text_equal(response.id, "org.kurogane.about")) ++saw_about;
        if (text_equal(response.id, "org.kurogane.missing") ||
            text_equal(response.id, "org.kurogane.toolong")) {
            fail(7U);
        }
    }
    if (saw_probe != 1U || saw_duplicate != 1U || saw_about != 1U) fail(8U);

    if (lookup(connection, "org.kurogane.probe", &response) != KU_STATUS_OK ||
        response.status != KU_STATUS_OK ||
        !text_equal(response.name, "Registry Probe") ||
        !text_equal(response.executable, "/apps/hello") ||
        response.manifest_version != 1U) {
        fail(9U);
    }

    if (lookup(connection, "org.kurogane.unknown", &response) != KU_STATUS_OK ||
        response.status != KU_STATUS_NOT_FOUND) {
        fail(10U);
    }

    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_APPLICATION_LOOKUP;
    if (transact(connection, &request, &response) != KU_STATUS_OK ||
        response.status != KU_STATUS_INVALID_ARGUMENT) {
        fail(11U);
    }

    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_APPLICATION_GET_BY_INDEX;
    request.index = count;
    if (transact(connection, &request, &response) != KU_STATUS_OK ||
        response.status != KU_STATUS_NOT_FOUND) {
        fail(12U);
    }

    if (ku_service_close(connection) != KU_STATUS_OK) fail(13U);
    (void)u_puts("[TEST] app_registry: PASS\n");
    ku_exit(0);
}

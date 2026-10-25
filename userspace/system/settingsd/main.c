#include "../../runtime/user.h"

#include <kurogane/event_broker.h>
#include <kurogane/filesystem.h>
#include <kurogane/settings.h>

#define SETTINGSD_MAX_CLIENTS 8U
#define SETTINGSD_MAX_RECORDS 32U
#define SETTINGSD_DB_PATH "/settings.db"
#define SETTINGSD_TMP_PATH "/settings.tmp"
#define SETTINGSD_DB_MAGIC UINT32_C(0x4B535431)
#define SETTINGSD_DB_VERSION UINT32_C(1)

typedef struct settingsd_client {
    ku_service_connection_t connection;
    uint64_t pid;
    int active;
} settingsd_client;

typedef struct settingsd_record {
    uint32_t type;
    uint32_t value_size;
    char key[KU_SETTINGS_KEY_CAPACITY];
    uint8_t value[KU_SETTINGS_VALUE_CAPACITY];
    int active;
} settingsd_record;

typedef struct settingsd_disk_header {
    uint32_t magic;
    uint32_t version;
    uint32_t record_count;
    uint32_t checksum;
} settingsd_disk_header;

typedef struct settingsd_disk_record {
    uint32_t type;
    uint32_t value_size;
    char key[KU_SETTINGS_KEY_CAPACITY];
    uint8_t value[KU_SETTINGS_VALUE_CAPACITY];
} settingsd_disk_record;

static settingsd_client clients[SETTINGSD_MAX_CLIENTS];
static settingsd_record records[SETTINGSD_MAX_RECORDS];

static void clear_bytes(void* memory, size_t size) {
    size_t index;
    uint8_t* bytes = (uint8_t*)memory;
    for (index = 0U; index < size; ++index) bytes[index] = 0U;
}

static void copy_bytes(void* destination, const void* source, size_t size) {
    size_t index;
    uint8_t* output = (uint8_t*)destination;
    const uint8_t* input = (const uint8_t*)source;
    for (index = 0U; index < size; ++index) output[index] = input[index];
}

static int key_character_valid(char value) {
    return (value >= 'a' && value <= 'z') ||
        (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9') ||
        value == '.' || value == '_' || value == '-' || value == '/';
}

static int key_valid(const char* key) {
    size_t index = 0U;
    if (key == (const char*)0 || key[0] == '\0') return 0;
    while (index < KU_SETTINGS_KEY_CAPACITY) {
        if (key[index] == '\0') return 1;
        if (!key_character_valid(key[index])) return 0;
        ++index;
    }
    return 0;
}

static int key_equal(const char* left, const char* right) {
    size_t index;
    for (index = 0U; index < KU_SETTINGS_KEY_CAPACITY; ++index) {
        if (left[index] != right[index]) return 0;
        if (left[index] == '\0') return 1;
    }
    return 0;
}

static void key_copy(char* destination, const char* source) {
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

static int value_valid(uint32_t type, uint32_t size, const uint8_t* value) {
    if (value == (const uint8_t*)0) return 0;
    switch (type) {
        case KU_SETTINGS_TYPE_BOOL:
            return size == 1U && value[0] <= 1U;
        case KU_SETTINGS_TYPE_I64:
        case KU_SETTINGS_TYPE_U64:
            return size == sizeof(uint64_t);
        case KU_SETTINGS_TYPE_STRING:
            return size > 0U && size <= KU_SETTINGS_VALUE_CAPACITY && value[size - 1U] == 0U;
        default:
            return 0;
    }
}

static settingsd_record* find_record(const char* key) {
    size_t index;
    for (index = 0U; index < SETTINGSD_MAX_RECORDS; ++index) {
        if (records[index].active && key_equal(records[index].key, key)) return &records[index];
    }
    return (settingsd_record*)0;
}

static settingsd_record* reserve_record(void) {
    size_t index;
    for (index = 0U; index < SETTINGSD_MAX_RECORDS; ++index) {
        if (!records[index].active) return &records[index];
    }
    return (settingsd_record*)0;
}

static uint32_t checksum_bytes(uint32_t checksum, const void* data, size_t size) {
    const uint8_t* bytes = (const uint8_t*)data;
    size_t index;
    for (index = 0U; index < size; ++index) {
        checksum ^= bytes[index];
        checksum *= UINT32_C(16777619);
    }
    return checksum;
}

static int write_all(ku_file_t file, const void* data, size_t size) {
    const uint8_t* bytes = (const uint8_t*)data;
    size_t offset = 0U;
    while (offset < size) {
        const ku_result_t written = ku_file_write(file, bytes + offset, size - offset);
        if (written <= 0) return 0;
        offset += (size_t)written;
    }
    return 1;
}

static int read_all(ku_file_t file, void* data, size_t size) {
    uint8_t* bytes = (uint8_t*)data;
    size_t offset = 0U;
    while (offset < size) {
        const ku_result_t received = ku_file_read(file, bytes + offset, size - offset);
        if (received <= 0) return 0;
        offset += (size_t)received;
    }
    return 1;
}

static ku_status_t persist_records(void) {
    settingsd_disk_header header;
    settingsd_disk_record disk_records[SETTINGSD_MAX_RECORDS];
    uint32_t count = 0U;
    uint32_t checksum = UINT32_C(2166136261);
    size_t index;
    ku_result_t opened;
    ku_file_t file;
    ku_status_t status;

    clear_bytes(disk_records, sizeof(disk_records));
    for (index = 0U; index < SETTINGSD_MAX_RECORDS; ++index) {
        settingsd_disk_record* output;
        if (!records[index].active) continue;
        output = &disk_records[count++];
        output->type = records[index].type;
        output->value_size = records[index].value_size;
        key_copy(output->key, records[index].key);
        copy_bytes(output->value, records[index].value, records[index].value_size);
    }
    checksum = checksum_bytes(checksum, disk_records, (size_t)count * sizeof(settingsd_disk_record));
    header.magic = SETTINGSD_DB_MAGIC;
    header.version = SETTINGSD_DB_VERSION;
    header.record_count = count;
    header.checksum = checksum;

    status = ku_file_unlink(SETTINGSD_TMP_PATH, sizeof(SETTINGSD_TMP_PATH) - 1U);
    if (status != KU_STATUS_OK && status != KU_STATUS_NOT_FOUND) return status;
    status = ku_file_create(SETTINGSD_TMP_PATH, sizeof(SETTINGSD_TMP_PATH) - 1U);
    if (status != KU_STATUS_OK) return status;
    opened = ku_file_open_ex(
        SETTINGSD_TMP_PATH,
        sizeof(SETTINGSD_TMP_PATH) - 1U,
        KU_FILE_OPEN_WRITE);
    if (opened <= 0) {
        (void)ku_file_unlink(SETTINGSD_TMP_PATH, sizeof(SETTINGSD_TMP_PATH) - 1U);
        return (ku_status_t)opened;
    }
    file = (ku_file_t)opened;
    if (!write_all(file, &header, sizeof(header)) ||
        (count != 0U && !write_all(file, disk_records, (size_t)count * sizeof(settingsd_disk_record)))) {
        (void)ku_file_close(file);
        (void)ku_file_unlink(SETTINGSD_TMP_PATH, sizeof(SETTINGSD_TMP_PATH) - 1U);
        return KU_STATUS_IO_ERROR;
    }
    status = ku_file_close(file);
    if (status != KU_STATUS_OK) {
        (void)ku_file_unlink(SETTINGSD_TMP_PATH, sizeof(SETTINGSD_TMP_PATH) - 1U);
        return status;
    }
    status = ku_file_sync();
    if (status != KU_STATUS_OK) return status;

    status = ku_file_rename(
        SETTINGSD_TMP_PATH,
        sizeof(SETTINGSD_TMP_PATH) - 1U,
        SETTINGSD_DB_PATH,
        sizeof(SETTINGSD_DB_PATH) - 1U);
    if (status == KU_STATUS_ALREADY_EXISTS) {
        status = ku_file_unlink(SETTINGSD_DB_PATH, sizeof(SETTINGSD_DB_PATH) - 1U);
        if (status == KU_STATUS_OK || status == KU_STATUS_NOT_FOUND) {
            status = ku_file_rename(
                SETTINGSD_TMP_PATH,
                sizeof(SETTINGSD_TMP_PATH) - 1U,
                SETTINGSD_DB_PATH,
                sizeof(SETTINGSD_DB_PATH) - 1U);
        }
    }
    if (status != KU_STATUS_OK) {
        (void)ku_file_unlink(SETTINGSD_TMP_PATH, sizeof(SETTINGSD_TMP_PATH) - 1U);
        return status;
    }
    return ku_file_sync();
}

static ku_status_t load_records(void) {
    settingsd_disk_header header;
    settingsd_disk_record disk_records[SETTINGSD_MAX_RECORDS];
    ku_file_stat info;
    ku_result_t opened;
    ku_file_t file;
    ku_status_t status;
    uint32_t checksum;
    size_t expected_size;
    size_t index;

    clear_bytes(records, sizeof(records));
    status = ku_file_stat_path(SETTINGSD_DB_PATH, sizeof(SETTINGSD_DB_PATH) - 1U, &info);
    if (status == KU_STATUS_NOT_FOUND) return KU_STATUS_OK;
    if (status != KU_STATUS_OK || info.type != KU_FILE_TYPE_REGULAR) return KU_STATUS_IO_ERROR;
    if (info.size < sizeof(header) || info.size > sizeof(header) + sizeof(disk_records)) return KU_STATUS_CORRUPT_DATA;

    opened = ku_file_open(SETTINGSD_DB_PATH, sizeof(SETTINGSD_DB_PATH) - 1U);
    if (opened <= 0) return (ku_status_t)opened;
    file = (ku_file_t)opened;
    if (!read_all(file, &header, sizeof(header))) {
        (void)ku_file_close(file);
        return KU_STATUS_IO_ERROR;
    }
    if (header.magic != SETTINGSD_DB_MAGIC || header.version != SETTINGSD_DB_VERSION ||
        header.record_count > SETTINGSD_MAX_RECORDS) {
        (void)ku_file_close(file);
        return KU_STATUS_CORRUPT_DATA;
    }
    expected_size = sizeof(header) + (size_t)header.record_count * sizeof(settingsd_disk_record);
    if (info.size != expected_size) {
        (void)ku_file_close(file);
        return KU_STATUS_CORRUPT_DATA;
    }
    clear_bytes(disk_records, sizeof(disk_records));
    if (header.record_count != 0U &&
        !read_all(file, disk_records, (size_t)header.record_count * sizeof(settingsd_disk_record))) {
        (void)ku_file_close(file);
        return KU_STATUS_IO_ERROR;
    }
    status = ku_file_close(file);
    if (status != KU_STATUS_OK) return status;

    checksum = checksum_bytes(
        UINT32_C(2166136261),
        disk_records,
        (size_t)header.record_count * sizeof(settingsd_disk_record));
    if (checksum != header.checksum) return KU_STATUS_CORRUPT_DATA;

    for (index = 0U; index < header.record_count; ++index) {
        settingsd_disk_record* input = &disk_records[index];
        if (!key_valid(input->key) || !value_valid(input->type, input->value_size, input->value)) {
            clear_bytes(records, sizeof(records));
            return KU_STATUS_CORRUPT_DATA;
        }
        records[index].type = input->type;
        records[index].value_size = input->value_size;
        key_copy(records[index].key, input->key);
        copy_bytes(records[index].value, input->value, input->value_size);
        records[index].active = 1;
    }
    return KU_STATUS_OK;
}

static ku_status_t set_record(const ku_settings_request* request) {
    settingsd_record* record = find_record(request->key);
    settingsd_record previous;
    int had_previous = record != (settingsd_record*)0;
    ku_status_t status;

    if (record == (settingsd_record*)0) record = reserve_record();
    if (record == (settingsd_record*)0) return KU_STATUS_OUT_OF_MEMORY;
    previous = *record;
    clear_bytes(record, sizeof(*record));
    record->type = request->type;
    record->value_size = request->value_size;
    key_copy(record->key, request->key);
    copy_bytes(record->value, request->value, request->value_size);
    record->active = 1;
    status = persist_records();
    if (status != KU_STATUS_OK) {
        if (had_previous) *record = previous;
        else clear_bytes(record, sizeof(*record));
    }
    return status;
}

static ku_status_t delete_record(const char* key) {
    settingsd_record* record = find_record(key);
    settingsd_record previous;
    ku_status_t status;
    if (record == (settingsd_record*)0) return KU_STATUS_NOT_FOUND;
    previous = *record;
    clear_bytes(record, sizeof(*record));
    status = persist_records();
    if (status != KU_STATUS_OK) *record = previous;
    return status;
}

static ku_service_connection_t change_broker_connection;

static void copy_topic(char* destination, const char* source) {
    size_t index = 0U;
    while (index + 1U < KU_EVENT_BROKER_TOPIC_CAPACITY && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index++] = '\0';
    while (index < KU_EVENT_BROKER_TOPIC_CAPACITY) destination[index++] = '\0';
}

static ku_status_t connect_change_broker(void) {
    uint32_t attempt;
    ku_result_t connected;
    if (change_broker_connection != 0U) return KU_STATUS_OK;
    for (attempt = 0U; attempt < 200U; ++attempt) {
        connected = ku_event_broker_connect();
        if (connected > 0) {
            change_broker_connection = (ku_service_connection_t)connected;
            return KU_STATUS_OK;
        }
        if (connected != KU_STATUS_NOT_FOUND && connected != KU_STATUS_WOULD_BLOCK)
            return (ku_status_t)connected;
        (void)ku_sleep(1U);
    }
    return KU_STATUS_TIMED_OUT;
}

static ku_status_t wait_change_publish_response(void) {
    uint32_t attempt;
    for (attempt = 0U; attempt < 200U; ++attempt) {
        ku_service_message message;
        ku_event_broker_response response;
        ku_status_t status = ku_service_receive(change_broker_connection, &message);
        if (status == KU_STATUS_WOULD_BLOCK) {
            (void)ku_sleep(1U);
            continue;
        }
        if (status != KU_STATUS_OK) return status;
        if (message.data_size != sizeof(response)) return KU_STATUS_CORRUPT_DATA;
        response = *(const ku_event_broker_response*)(const void*)message.data;
        if (response.structure_size != sizeof(response)) return KU_STATUS_CORRUPT_DATA;
        return (ku_status_t)response.status;
    }
    return KU_STATUS_TIMED_OUT;
}

static ku_status_t publish_change_event(void) {
    ku_event_broker_request request;
    ku_status_t status = connect_change_broker();
    if (status != KU_STATUS_OK) return status;
    clear_bytes(&request, sizeof(request));
    request.structure_size = sizeof(request);
    request.operation = KU_EVENT_BROKER_PUBLISH;
    copy_topic(request.topic, KU_SETTINGS_CHANGED_TOPIC);
    status = ku_service_send(change_broker_connection, &request, sizeof(request));
    if (status == KU_STATUS_OK) status = wait_change_publish_response();
    if (status != KU_STATUS_OK) {
        (void)ku_service_close(change_broker_connection);
        change_broker_connection = 0U;
    }
    return status;
}

static ku_status_t send_response(
    ku_service_connection_t connection,
    ku_status_t status,
    const settingsd_record* record) {
    ku_settings_response response;
    clear_bytes(&response, sizeof(response));
    response.structure_size = sizeof(response);
    response.status = status;
    if (status == KU_STATUS_OK && record != (const settingsd_record*)0) {
        response.type = record->type;
        response.value_size = record->value_size;
        copy_bytes(response.value, record->value, record->value_size);
    }
    return ku_service_send(connection, &response, sizeof(response));
}

static void handle_request(settingsd_client* client, const ku_service_message* message) {
    const ku_settings_request* request;
    settingsd_record* record = (settingsd_record*)0;
    ku_status_t status = KU_STATUS_INVALID_ARGUMENT;

    if (message->data_size != sizeof(ku_settings_request)) {
        (void)send_response(client->connection, KU_STATUS_CORRUPT_DATA, (const settingsd_record*)0);
        return;
    }
    request = (const ku_settings_request*)(const void*)message->data;
    if (request->structure_size != sizeof(*request) || !key_valid(request->key)) {
        (void)send_response(client->connection, KU_STATUS_INVALID_ARGUMENT, (const settingsd_record*)0);
        return;
    }
    if (client->pid == 0U) client->pid = message->sender_pid;
    else if (client->pid != message->sender_pid) {
        (void)send_response(client->connection, KU_STATUS_ACCESS_DENIED, (const settingsd_record*)0);
        return;
    }

    switch (request->operation) {
        case KU_SETTINGS_GET:
            if (request->type != KU_SETTINGS_TYPE_NONE || request->value_size != 0U) {
                status = KU_STATUS_INVALID_ARGUMENT;
                break;
            }
            record = find_record(request->key);
            status = record == (settingsd_record*)0 ? KU_STATUS_NOT_FOUND : KU_STATUS_OK;
            break;
        case KU_SETTINGS_SET:
            if (!value_valid(request->type, request->value_size, request->value)) {
                status = KU_STATUS_INVALID_ARGUMENT;
                break;
            }
            status = set_record(request);
            break;
        case KU_SETTINGS_DELETE:
            if (request->type != KU_SETTINGS_TYPE_NONE || request->value_size != 0U) {
                status = KU_STATUS_INVALID_ARGUMENT;
                break;
            }
            status = delete_record(request->key);
            break;
        default:
            status = KU_STATUS_NOT_SUPPORTED;
            break;
    }
    if (status == KU_STATUS_OK &&
        (request->operation == KU_SETTINGS_SET || request->operation == KU_SETTINGS_DELETE)) {
        const ku_status_t change_status = publish_change_event();
        (void)u_puts(change_status == KU_STATUS_OK
            ? "[TEST] settings_change_publish: PASS\n"
            : "[TEST] settings_change_publish: FAIL\n");
    }
    (void)send_response(client->connection, status, record);
}

static void accept_clients(ku_service_endpoint_t endpoint) {
    for (;;) {
        ku_result_t accepted = ku_service_accept(endpoint);
        size_t index;
        if (accepted == KU_STATUS_WOULD_BLOCK) return;
        if (accepted <= 0) return;
        for (index = 0U; index < SETTINGSD_MAX_CLIENTS; ++index) {
            if (!clients[index].active) {
                clients[index].connection = (ku_service_connection_t)accepted;
                clients[index].pid = 0U;
                clients[index].active = 1;
                break;
            }
        }
        if (index == SETTINGSD_MAX_CLIENTS) {
            (void)ku_service_close((ku_service_connection_t)accepted);
            return;
        }
    }
}

static void service_clients(void) {
    size_t index;
    for (index = 0U; index < SETTINGSD_MAX_CLIENTS; ++index) {
        settingsd_client* client = &clients[index];
        ku_service_message message;
        ku_status_t status;
        if (!client->active) continue;
        status = ku_service_receive(client->connection, &message);
        if (status == KU_STATUS_WOULD_BLOCK) continue;
        if (status != KU_STATUS_OK) {
            (void)ku_service_close(client->connection);
            clear_bytes(client, sizeof(*client));
            continue;
        }
        handle_request(client, &message);
    }
}

__attribute__((noreturn)) void _start(void) {
    const ku_status_t load_status = load_records();
    ku_result_t endpoint;
    if (load_status != KU_STATUS_OK) {
        (void)u_puts("settingsd: persistent store invalid\n");
        (void)u_puts("[TEST] settings_service_store_load: FAIL\n");
        ku_exit(1);
    }
    (void)u_puts("[TEST] settings_service_store_load: PASS\n");

    endpoint = ku_service_register(KU_SETTINGS_SERVICE_NAME, KU_SETTINGS_SERVICE_NAME_SIZE);
    if (endpoint <= 0) {
        (void)u_puts("settingsd: service registration failed\n");
        (void)u_puts("[TEST] settings_service_online: FAIL\n");
        ku_exit(2);
    }
    (void)u_puts("settingsd: settings.v1 online\n");
    (void)u_puts("[TEST] settings_service_online: PASS\n");

    for (;;) {
        accept_clients((ku_service_endpoint_t)endpoint);
        service_clients();
        (void)ku_sleep(1U);
    }
}

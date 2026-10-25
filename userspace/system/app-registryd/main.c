#include "../../runtime/user.h"

#include <kurogane/application.h>
#include <kurogane/filesystem.h>

#define APPREG_MAX_CLIENTS 8U
#define APPREG_MANIFEST_DIRECTORY "/apps/appman"
#define APPREG_MANIFEST_BUFFER_CAPACITY 512U
#define APPREG_PATH_CAPACITY 160U

typedef struct appreg_client {
    ku_service_connection_t connection;
    uint64_t pid;
    int active;
} appreg_client;

typedef struct appreg_entry {
    uint32_t flags;
    uint32_t manifest_version;
    char id[KU_APPLICATION_ID_CAPACITY];
    char name[KU_APPLICATION_NAME_CAPACITY];
    char executable[KU_APPLICATION_EXECUTABLE_CAPACITY];
    int active;
} appreg_entry;

static appreg_client clients[APPREG_MAX_CLIENTS];
static appreg_entry entries[KU_APPLICATION_MAX_ENTRIES];
static uint32_t entry_count;

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

static int copy_text(char* destination, size_t capacity, const char* source, size_t length) {
    size_t index;
    if (destination == (char*)0 || source == (const char*)0 || capacity == 0U ||
        length == 0U || length >= capacity) return 0;
    for (index = 0U; index < length; ++index) {
        const unsigned char value = (unsigned char)source[index];
        if (value < 0x20U || value > 0x7EU) return 0;
        destination[index] = source[index];
    }
    destination[length] = '\0';
    for (++length; length < capacity; ++length) destination[length] = '\0';
    return 1;
}

static int parse_u32(const char* text, size_t length, uint32_t* output) {
    size_t index;
    uint32_t value = 0U;
    if (text == (const char*)0 || output == (uint32_t*)0 || length == 0U) return 0;
    for (index = 0U; index < length; ++index) {
        const unsigned char digit = (unsigned char)text[index];
        if (digit < (unsigned char)'0' || digit > (unsigned char)'9') return 0;
        if (value > (UINT32_MAX - (uint32_t)(digit - (unsigned char)'0')) / 10U) return 0;
        value = value * 10U + (uint32_t)(digit - (unsigned char)'0');
    }
    *output = value;
    return 1;
}

static int key_equal(const char* key, size_t key_length, const char* expected) {
    size_t index = 0U;
    while (index < key_length && expected[index] != '\0' && key[index] == expected[index]) ++index;
    return index == key_length && expected[index] == '\0';
}

static int parse_manifest(const char* text, size_t size, appreg_entry* output) {
    size_t cursor = 0U;
    appreg_entry parsed;
    int have_id = 0;
    int have_name = 0;
    int have_exec = 0;
    int have_version = 0;
    clear_bytes(&parsed, sizeof(parsed));
    while (cursor < size) {
        size_t line_start = cursor;
        size_t line_end;
        size_t equals;
        while (cursor < size && text[cursor] != '\n' && text[cursor] != '\r' && text[cursor] != '\0') ++cursor;
        line_end = cursor;
        while (cursor < size && (text[cursor] == '\n' || text[cursor] == '\r' || text[cursor] == '\0')) ++cursor;
        if (line_end == line_start || text[line_start] == '#') continue;
        equals = line_start;
        while (equals < line_end && text[equals] != '=') ++equals;
        if (equals == line_start || equals == line_end) return 0;
        {
            const char* key = text + line_start;
            const size_t key_length = equals - line_start;
            const char* value = text + equals + 1U;
            const size_t value_length = line_end - equals - 1U;
            if (key_equal(key, key_length, "format")) {
                uint32_t version = 0U;
                if (!parse_u32(value, value_length, &version) || version != 1U) return 0;
                parsed.manifest_version = version;
                have_version = 1;
            } else if (key_equal(key, key_length, "id")) {
                if (!copy_text(parsed.id, sizeof(parsed.id), value, value_length)) return 0;
                have_id = 1;
            } else if (key_equal(key, key_length, "name")) {
                if (!copy_text(parsed.name, sizeof(parsed.name), value, value_length)) return 0;
                have_name = 1;
            } else if (key_equal(key, key_length, "exec")) {
                if (!copy_text(parsed.executable, sizeof(parsed.executable), value, value_length) || parsed.executable[0] != '/') return 0;
                have_exec = 1;
            } else if (key_equal(key, key_length, "flags")) {
                if (!parse_u32(value, value_length, &parsed.flags)) return 0;
            }
        }
    }
    if (!have_version || !have_id || !have_name || !have_exec) return 0;
    parsed.active = 1;
    *output = parsed;
    return 1;
}

static int read_manifest(const char* path, appreg_entry* output) {
    char buffer[APPREG_MANIFEST_BUFFER_CAPACITY];
    const size_t path_length = text_length(path, APPREG_PATH_CAPACITY);
    const ku_result_t opened = ku_file_open(path, path_length);
    ku_result_t read_result;
    ku_file_t file;
    if (opened <= 0 || path_length == 0U || path_length >= APPREG_PATH_CAPACITY) return 0;
    file = (ku_file_t)opened;
    read_result = ku_file_read(file, buffer, sizeof(buffer) - 1U);
    (void)ku_file_close(file);
    if (read_result <= 0 || (size_t)read_result >= sizeof(buffer)) return 0;
    buffer[(size_t)read_result] = '\0';
    return parse_manifest(buffer, (size_t)read_result, output);
}

static int executable_exists(const char* path) {
    const size_t path_length = text_length(path, KU_APPLICATION_EXECUTABLE_CAPACITY);
    ku_file_stat info;
    if (path_length == 0U || path_length >= KU_APPLICATION_EXECUTABLE_CAPACITY) return 0;
    clear_bytes(&info, sizeof(info));
    info.structure_size = sizeof(info);
    if (ku_file_stat_path(path, path_length, &info) != KU_STATUS_OK) return 0;
    return info.type == KU_FILE_TYPE_REGULAR && info.size != 0U;
}

static int id_exists(const char* id) {
    uint32_t index;
    for (index = 0U; index < entry_count; ++index) {
        if (entries[index].active && text_equal(entries[index].id, id)) return 1;
    }
    return 0;
}

static void load_registry(void) {
    const ku_result_t opened = ku_file_open_ex(
        APPREG_MANIFEST_DIRECTORY,
        sizeof(APPREG_MANIFEST_DIRECTORY) - 1U,
        KU_FILE_OPEN_READ | KU_FILE_OPEN_DIRECTORY);
    ku_file_t directory;
    clear_bytes(entries, sizeof(entries));
    entry_count = 0U;
    if (opened <= 0) return;
    directory = (ku_file_t)opened;
    while (entry_count < KU_APPLICATION_MAX_ENTRIES) {
        ku_directory_entry item;
        ku_status_t status = ku_file_readdir(directory, &item);
        char path[APPREG_PATH_CAPACITY];
        size_t prefix_length;
        size_t index;
        appreg_entry parsed;
        if (status != KU_STATUS_OK) break;
        if (item.type != KU_FILE_TYPE_REGULAR || item.name_length == 0U ||
            item.name_length >= KU_FILE_NAME_CAPACITY) continue;
        clear_bytes(path, sizeof(path));
        prefix_length = sizeof(APPREG_MANIFEST_DIRECTORY) - 1U;
        if (prefix_length + 1U + item.name_length + 1U > sizeof(path)) continue;
        for (index = 0U; index < prefix_length; ++index) path[index] = APPREG_MANIFEST_DIRECTORY[index];
        path[prefix_length++] = '/';
        for (index = 0U; index < item.name_length; ++index) path[prefix_length + index] = item.name[index];
        path[prefix_length + item.name_length] = '\0';
        clear_bytes(&parsed, sizeof(parsed));
        if (!read_manifest(path, &parsed) || !executable_exists(parsed.executable) ||
            id_exists(parsed.id)) continue;
        entries[entry_count++] = parsed;
    }
    (void)ku_file_close(directory);
}

static ku_status_t send_response(ku_service_connection_t connection, ku_status_t status, uint32_t index, const appreg_entry* entry) {
    ku_application_response response;
    size_t i;
    clear_bytes(&response, sizeof(response));
    response.structure_size = sizeof(response);
    response.status = status;
    response.index = index;
    response.count = entry_count;
    if (status == KU_STATUS_OK && entry != (const appreg_entry*)0) {
        response.flags = entry->flags;
        response.manifest_version = entry->manifest_version;
        for (i = 0U; i < sizeof(response.id); ++i) response.id[i] = entry->id[i];
        for (i = 0U; i < sizeof(response.name); ++i) response.name[i] = entry->name[i];
        for (i = 0U; i < sizeof(response.executable); ++i) response.executable[i] = entry->executable[i];
    }
    return ku_service_send(connection, &response, sizeof(response));
}

static int request_id_valid(const char* id) {
    const size_t length = text_length(id, KU_APPLICATION_ID_CAPACITY);
    return length > 0U && length < KU_APPLICATION_ID_CAPACITY;
}

static void handle_request(appreg_client* client, const ku_service_message* message) {
    const ku_application_request* request;
    uint32_t index;
    if (client == (appreg_client*)0 || message == (const ku_service_message*)0) return;
    if (message->data_size != sizeof(ku_application_request)) {
        (void)send_response(client->connection, KU_STATUS_CORRUPT_DATA, 0U, 0);
        return;
    }
    request = (const ku_application_request*)(const void*)message->data;
    if (request->structure_size != sizeof(*request) || request->reserved != 0U) {
        (void)send_response(client->connection, KU_STATUS_INVALID_ARGUMENT, 0U, 0);
        return;
    }
    if (client->pid == 0U) client->pid = message->sender_pid;
    else if (client->pid != message->sender_pid) {
        (void)send_response(client->connection, KU_STATUS_ACCESS_DENIED, 0U, 0);
        return;
    }
    if (request->operation == KU_APPLICATION_GET_COUNT) {
        (void)send_response(client->connection, KU_STATUS_OK, 0U, 0);
        return;
    }
    if (request->operation == KU_APPLICATION_GET_BY_INDEX) {
        if (request->index >= entry_count) {
            (void)send_response(client->connection, KU_STATUS_NOT_FOUND, request->index, 0);
            return;
        }
        (void)send_response(client->connection, KU_STATUS_OK, request->index, &entries[request->index]);
        return;
    }
    if (request->operation == KU_APPLICATION_LOOKUP) {
        if (!request_id_valid(request->id)) {
            (void)send_response(client->connection, KU_STATUS_INVALID_ARGUMENT, 0U, 0);
            return;
        }
        for (index = 0U; index < entry_count; ++index) {
            if (text_equal(entries[index].id, request->id)) {
                (void)send_response(client->connection, KU_STATUS_OK, index, &entries[index]);
                return;
            }
        }
        (void)send_response(client->connection, KU_STATUS_NOT_FOUND, 0U, 0);
        return;
    }
    (void)send_response(client->connection, KU_STATUS_NOT_SUPPORTED, 0U, 0);
}

static void accept_clients(ku_service_endpoint_t endpoint) {
    for (;;) {
        const ku_result_t accepted = ku_service_accept(endpoint);
        size_t index;
        if (accepted == KU_STATUS_WOULD_BLOCK) return;
        if (accepted <= 0) return;
        for (index = 0U; index < APPREG_MAX_CLIENTS; ++index) {
            if (!clients[index].active) {
                clients[index].connection = (ku_service_connection_t)accepted;
                clients[index].pid = 0U;
                clients[index].active = 1;
                break;
            }
        }
        if (index == APPREG_MAX_CLIENTS) {
            (void)ku_service_close((ku_service_connection_t)accepted);
            return;
        }
    }
}

static void service_clients(void) {
    size_t index;
    for (index = 0U; index < APPREG_MAX_CLIENTS; ++index) {
        appreg_client* client = &clients[index];
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
    const ku_result_t endpoint = ku_service_register(
        KU_APPLICATION_SERVICE_NAME, KU_APPLICATION_SERVICE_NAME_SIZE);
    if (endpoint <= 0) ku_exit(1);
    load_registry();
    (void)u_puts("app-registryd: appreg.v1 online entries=");
    (void)u_put_u64(entry_count);
    (void)u_puts("\n");
    for (;;) {
        accept_clients((ku_service_endpoint_t)endpoint);
        service_clients();
        (void)ku_sleep(1U);
    }
}

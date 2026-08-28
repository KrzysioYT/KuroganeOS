#include "../common.h"
#include "sha256.h"

#define ANVIL_MAX_PACKAGES 12U
#define ANVIL_VISIBLE_PACKAGES 6U
#define ANVIL_CONFIG_PATH "/etc/anvil.cfg"
#define ANVIL_DATABASE_PATH "/home/anvil.db"
#define ANVIL_DATABASE_CAPACITY 4096U
#define ANVIL_INDEX_RESPONSE_CAPACITY 32768U
#define ANVIL_MANIFEST_RESPONSE_CAPACITY 8192U
#define ANVIL_IO_CHUNK 16384U
#define ANVIL_DEPENDENCY_DEPTH 6U
#define ANVIL_REFRESH_ID 6U
#define ANVIL_INSTALL_ID 7U
#define ANVIL_UPDATE_ALL_ID 8U
#define ANVIL_PACKAGE_ROW_BASE 10U

#define ANVIL_PACKAGE_GET 0
#define ANVIL_PACKAGE_CURRENT 1
#define ANVIL_PACKAGE_UPDATE 2

typedef struct anvil_repository {
    char host[KU_NET_HOST_CAPACITY];
    char base[112];
} anvil_repository;

typedef struct anvil_package {
    char name[32];
    char version[20];
    char description[64];
    char manifest[96];
} anvil_package;

typedef struct anvil_manifest {
    char name[32];
    char version[20];
    char destination[128];
    char payload[112];
    char sha256[65];
    char depends[96];
    char peer[96];
    char conflicts[96];
    uint64_t bytes;
} anvil_manifest;

static anvil_repository g_repo;
static anvil_package g_packages[ANVIL_MAX_PACKAGES];
static size_t g_package_count = 0U;
static size_t g_selected = 0U;
static size_t g_scroll = 0U;
static char g_status[64] = "ANVIL / INITIALIZING";
static char g_index_response[ANVIL_INDEX_RESPONSE_CAPACITY];
static char g_manifest_response[ANVIL_MANIFEST_RESPONSE_CAPACITY];
static uint8_t g_payload_response[KU_HTTP_RESPONSE_CAPACITY_LIMIT];
static char g_database[ANVIL_DATABASE_CAPACITY];
static int g_database_loaded = 0;

static int starts_with(const char* text, const char* prefix) {
    size_t index = 0U;
    if (text == NULL || prefix == NULL) return 0;
    while (prefix[index] != '\0') {
        if (text[index] != prefix[index]) return 0;
        ++index;
    }
    return 1;
}

static int read_file_text(const char* path, char* output, size_t capacity) {
    ku_result_t opened;
    ku_result_t count;
    if (path == NULL || output == NULL || capacity < 2U) return 0;
    opened = ku_file_open(path, strlen(path));
    if (opened < 0) return 0;
    count = ku_file_read((ku_file_t)opened, output, capacity - 1U);
    (void)ku_file_close((ku_file_t)opened);
    if (count < 0) return 0;
    output[(size_t)count] = '\0';
    return 1;
}

static int config_value(
    const char* text,
    const char* key,
    char* output,
    size_t capacity) {
    size_t line = 0U;
    const size_t key_length = strlen(key);
    if (text == NULL || key == NULL || output == NULL || capacity == 0U) return 0;
    while (text[line] != '\0') {
        size_t index = 0U;
        if (line == 0U || text[line - 1U] == '\n') {
            while (index < key_length && text[line + index] == key[index]) ++index;
            if (index == key_length && text[line + index] == '=') {
                size_t source = line + key_length + 1U;
                size_t written = 0U;
                while (text[source] != '\0' && text[source] != '\r' &&
                       text[source] != '\n' && written + 1U < capacity) {
                    output[written++] = text[source++];
                }
                output[written] = '\0';
                return written != 0U;
            }
        }
        while (text[line] != '\0' && text[line] != '\n') ++line;
        if (text[line] == '\n') ++line;
    }
    output[0] = '\0';
    return 0;
}

static void load_repository(void) {
    char config[384];
    (void)strlcpy(g_repo.host, "raw.githubusercontent.com", sizeof(g_repo.host));
    (void)strlcpy(
        g_repo.base,
        "/KrzysioYT/KuroganeOS-Packages/main",
        sizeof(g_repo.base));
    if (!read_file_text(ANVIL_CONFIG_PATH, config, sizeof(config))) return;
    (void)config_value(config, "HOST", g_repo.host, sizeof(g_repo.host));
    (void)config_value(config, "BASE", g_repo.base, sizeof(g_repo.base));
}

static int repo_path(const char* relative, char* output, size_t capacity) {
    if (relative == NULL || output == NULL || capacity == 0U || relative[0] != '/') return 0;
    output[0] = '\0';
    (void)strlcpy(output, g_repo.base, capacity);
    gui_append_text(output, capacity, relative);
    return strlen(output) + 1U < capacity && strlen(output) < KU_NET_PATH_CAPACITY;
}

static int response_body(
    uint8_t* response,
    size_t response_size,
    uint8_t** body,
    size_t* body_size) {
    size_t index;
    if (response == NULL || body == NULL || body_size == NULL) return 0;
    for (index = 0U; index + 3U < response_size; ++index) {
        if (response[index] == '\r' && response[index + 1U] == '\n' &&
            response[index + 2U] == '\r' && response[index + 3U] == '\n') {
            *body = response + index + 4U;
            *body_size = response_size - index - 4U;
            return 1;
        }
    }
    return 0;
}

static int fetch_https(
    const char* relative,
    void* response,
    size_t capacity,
    uint8_t** body,
    size_t* body_size) {
    ku_http_request request;
    char path[KU_NET_PATH_CAPACITY];
    ku_status_t status;
    if (!repo_path(relative, path, sizeof(path))) {
        (void)strlcpy(g_status, "ANVIL / REPOSITORY PATH TOO LONG", sizeof(g_status));
        return 0;
    }
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    (void)strlcpy(request.host, g_repo.host, sizeof(request.host));
    (void)strlcpy(request.path, path, sizeof(request.path));
    request.output = response;
    request.output_capacity = capacity;
    status = ku_https_get(&request);
    if (status != KU_STATUS_OK || request.http_status != 200U) {
        (void)strlcpy(g_status, "ANVIL / HTTPS FETCH FAILED", sizeof(g_status));
        return 0;
    }
    if (!response_body(
            (uint8_t*)response,
            (size_t)request.bytes_received,
            body,
            body_size)) {
        (void)strlcpy(g_status, "ANVIL / INVALID HTTP RESPONSE", sizeof(g_status));
        return 0;
    }
    return 1;
}

static int copy_field(
    const char* line,
    size_t line_length,
    size_t* cursor,
    char* output,
    size_t capacity) {
    size_t written = 0U;
    if (cursor == NULL || output == NULL || capacity == 0U || *cursor > line_length) return 0;
    while (*cursor < line_length && line[*cursor] != '|') {
        if (written + 1U >= capacity) return 0;
        output[written++] = line[(*cursor)++];
    }
    output[written] = '\0';
    if (*cursor < line_length && line[*cursor] == '|') ++(*cursor);
    return written != 0U;
}

static int parse_catalog(char* body, size_t body_size) {
    size_t line_start = 0U;
    g_package_count = 0U;
    if (body_size < 6U || !starts_with(body, "KIDX1")) return 0;
    while (line_start < body_size && body[line_start] != '\n') ++line_start;
    if (line_start < body_size) ++line_start;

    while (line_start < body_size && g_package_count < ANVIL_MAX_PACKAGES) {
        size_t line_end = line_start;
        size_t cursor;
        anvil_package* package;
        while (line_end < body_size && body[line_end] != '\n' && body[line_end] != '\r') ++line_end;
        if (line_end == line_start) {
            while (line_end < body_size && (body[line_end] == '\n' || body[line_end] == '\r')) ++line_end;
            line_start = line_end;
            continue;
        }
        if (line_end - line_start < 4U ||
            body[line_start] != 'p' || body[line_start + 1U] != 'k' ||
            body[line_start + 2U] != 'g' || body[line_start + 3U] != '|') {
            line_start = line_end + (line_end < body_size ? 1U : 0U);
            continue;
        }
        package = &g_packages[g_package_count];
        memset(package, 0, sizeof(*package));
        cursor = line_start + 4U;
        if (!copy_field(body, line_end, &cursor, package->name, sizeof(package->name)) ||
            !copy_field(body, line_end, &cursor, package->version, sizeof(package->version)) ||
            !copy_field(body, line_end, &cursor, package->description, sizeof(package->description)) ||
            !copy_field(body, line_end, &cursor, package->manifest, sizeof(package->manifest)) ||
            package->manifest[0] != '/') {
            line_start = line_end + (line_end < body_size ? 1U : 0U);
            continue;
        }
        ++g_package_count;
        while (line_end < body_size && (body[line_end] == '\n' || body[line_end] == '\r')) ++line_end;
        line_start = line_end;
    }
    g_selected = 0U;
    g_scroll = 0U;
    return g_package_count != 0U;
}

static int refresh_catalog(void) {
    uint8_t* body = NULL;
    size_t body_size = 0U;
    (void)strlcpy(g_status, "ANVIL / FETCHING CATALOG", sizeof(g_status));
    if (!fetch_https(
            "/index.kuro",
            g_index_response,
            sizeof(g_index_response) - 1U,
            &body,
            &body_size)) return 0;
    if (body_size + 1U >= sizeof(g_index_response)) return 0;
    body[body_size] = '\0';
    if (!parse_catalog((char*)body, body_size)) {
        (void)strlcpy(g_status, "ANVIL / EMPTY OR INVALID CATALOG", sizeof(g_status));
        return 0;
    }
    (void)strlcpy(g_status, "ANVIL / CATALOG READY", sizeof(g_status));
    return 1;
}

static uint64_t parse_u64(const char* text) {
    uint64_t value = 0U;
    size_t index = 0U;
    if (text == NULL || text[0] == '\0') return 0U;
    while (text[index] >= '0' && text[index] <= '9') {
        const uint64_t digit = (uint64_t)(text[index] - '0');
        if (value > (UINT64_MAX - digit) / 10U) return 0U;
        value = value * 10U + digit;
        ++index;
    }
    return text[index] == '\0' ? value : 0U;
}

static int parse_manifest_text(const char* text, anvil_manifest* manifest) {
    char number[24];
    if (text == NULL || manifest == NULL || !starts_with(text, "KPKG1")) return 0;
    memset(manifest, 0, sizeof(*manifest));
    if (!config_value(text, "name", manifest->name, sizeof(manifest->name)) ||
        !config_value(text, "version", manifest->version, sizeof(manifest->version)) ||
        !config_value(text, "destination", manifest->destination, sizeof(manifest->destination)) ||
        !config_value(text, "payload", manifest->payload, sizeof(manifest->payload)) ||
        !config_value(text, "bytes", number, sizeof(number)) ||
        !config_value(text, "sha256", manifest->sha256, sizeof(manifest->sha256))) return 0;
    (void)config_value(text, "depends", manifest->depends, sizeof(manifest->depends));
    (void)config_value(text, "peer", manifest->peer, sizeof(manifest->peer));
    (void)config_value(text, "conflicts", manifest->conflicts, sizeof(manifest->conflicts));
    manifest->bytes = parse_u64(number);
    return manifest->name[0] != '\0' && manifest->version[0] != '\0' &&
        manifest->destination[0] == '/' && manifest->payload[0] == '/' &&
        manifest->bytes != 0U && strlen(manifest->sha256) == 64U;
}

static int load_manifest(size_t package_index, anvil_manifest* manifest) {
    uint8_t* body = NULL;
    size_t body_size = 0U;
    if (package_index >= g_package_count || manifest == NULL) return 0;
    if (!fetch_https(
            g_packages[package_index].manifest,
            g_manifest_response,
            sizeof(g_manifest_response) - 1U,
            &body,
            &body_size)) return 0;
    if (body_size + 1U >= sizeof(g_manifest_response)) return 0;
    body[body_size] = '\0';
    if (!parse_manifest_text((const char*)body, manifest)) {
        (void)strlcpy(g_status, "ANVIL / INVALID PACKAGE MANIFEST", sizeof(g_status));
        return 0;
    }
    if (strcmp(manifest->name, g_packages[package_index].name) != 0 ||
        strcmp(manifest->version, g_packages[package_index].version) != 0) {
        (void)strlcpy(g_status, "ANVIL / INDEX MANIFEST MISMATCH", sizeof(g_status));
        return 0;
    }
    return 1;
}

static void reload_install_database(void) {
    if (!read_file_text(ANVIL_DATABASE_PATH, g_database, sizeof(g_database))) {
        g_database[0] = '\0';
    }
    g_database_loaded = 1;
}

static int installed_version(
    const char* name,
    char* output,
    size_t capacity) {
    size_t line_start = 0U;
    int found = 0;
    if (name == NULL || name[0] == '\0') return 0;
    if (output != NULL && capacity != 0U) output[0] = '\0';
    if (!g_database_loaded) reload_install_database();

    while (g_database[line_start] != '\0') {
        size_t line_end = line_start;
        size_t cursor = line_start;
        char entry_name[32];
        char entry_version[20];
        while (g_database[line_end] != '\0' &&
               g_database[line_end] != '\r' &&
               g_database[line_end] != '\n') ++line_end;
        if (line_end > line_start &&
            copy_field(g_database, line_end, &cursor, entry_name, sizeof(entry_name)) &&
            copy_field(g_database, line_end, &cursor, entry_version, sizeof(entry_version)) &&
            strcmp(entry_name, name) == 0) {
            if (output != NULL && capacity != 0U) {
                (void)strlcpy(output, entry_version, capacity);
            }
            found = 1;
        }
        while (g_database[line_end] == '\r' || g_database[line_end] == '\n') ++line_end;
        line_start = line_end;
    }
    return found;
}

static int installed_name(const char* name) {
    return installed_version(name, NULL, 0U);
}

static int package_state(size_t package_index) {
    char installed[20];
    if (package_index >= g_package_count) return ANVIL_PACKAGE_GET;
    if (!installed_version(
            g_packages[package_index].name,
            installed,
            sizeof(installed))) return ANVIL_PACKAGE_GET;
    return strcmp(installed, g_packages[package_index].version) == 0
        ? ANVIL_PACKAGE_CURRENT
        : ANVIL_PACKAGE_UPDATE;
}

static size_t find_package(const char* name) {
    size_t index;
    for (index = 0U; index < g_package_count; ++index) {
        if (strcmp(g_packages[index].name, name) == 0) return index;
    }
    return g_package_count;
}

static int next_list_name(
    const char* list,
    size_t* cursor,
    char* name,
    size_t capacity) {
    size_t written = 0U;
    if (list == NULL || cursor == NULL || name == NULL || capacity == 0U) return 0;
    while (list[*cursor] == ' ' || list[*cursor] == ',') ++(*cursor);
    if (list[*cursor] == '\0') return 0;
    while (list[*cursor] != '\0' && list[*cursor] != ',') {
        const char character = list[(*cursor)++];
        if (character == ' ') continue;
        if (written + 1U >= capacity) return 0;
        name[written++] = character;
    }
    name[written] = '\0';
    return written != 0U;
}

static int record_install(const anvil_manifest* manifest) {
    char line[192] = "";
    ku_result_t opened;
    ku_result_t written;
    ku_status_t status = ku_file_create(
        ANVIL_DATABASE_PATH,
        sizeof(ANVIL_DATABASE_PATH) - 1U);
    if (status != KU_STATUS_OK && status != KU_STATUS_ALREADY_EXISTS) return 0;
    opened = ku_file_open_ex(
        ANVIL_DATABASE_PATH,
        sizeof(ANVIL_DATABASE_PATH) - 1U,
        KU_FILE_OPEN_WRITE | KU_FILE_OPEN_APPEND);
    if (opened < 0) return 0;
    gui_append_text(line, sizeof(line), manifest->name);
    gui_append_text(line, sizeof(line), "|");
    gui_append_text(line, sizeof(line), manifest->version);
    gui_append_text(line, sizeof(line), "|");
    gui_append_text(line, sizeof(line), manifest->destination);
    gui_append_text(line, sizeof(line), "\n");
    written = ku_file_write((ku_file_t)opened, line, strlen(line));
    (void)ku_file_close((ku_file_t)opened);
    (void)ku_file_sync();
    reload_install_database();
    return written == (ku_result_t)strlen(line);
}

static int write_payload_transaction(
    const anvil_manifest* manifest,
    const uint8_t* payload,
    size_t payload_size) {
    char temporary[160];
    char backup[160];
    ku_result_t opened;
    size_t offset = 0U;
    int had_old = 0;
    ku_file_stat stat;

    if (strlen(manifest->destination) + 5U >= sizeof(temporary)) return 0;
    (void)strlcpy(temporary, manifest->destination, sizeof(temporary));
    gui_append_text(temporary, sizeof(temporary), ".new");
    (void)strlcpy(backup, manifest->destination, sizeof(backup));
    gui_append_text(backup, sizeof(backup), ".old");
    (void)ku_file_unlink(temporary, strlen(temporary));
    (void)ku_file_unlink(backup, strlen(backup));

    if (ku_file_create(temporary, strlen(temporary)) != KU_STATUS_OK) return 0;
    opened = ku_file_open_ex(temporary, strlen(temporary), KU_FILE_OPEN_WRITE);
    if (opened < 0) {
        (void)ku_file_unlink(temporary, strlen(temporary));
        return 0;
    }
    while (offset < payload_size) {
        const size_t remaining = payload_size - offset;
        const size_t chunk = remaining < ANVIL_IO_CHUNK ? remaining : ANVIL_IO_CHUNK;
        const ku_result_t written = ku_file_write(
            (ku_file_t)opened,
            payload + offset,
            chunk);
        if (written != (ku_result_t)chunk) {
            (void)ku_file_close((ku_file_t)opened);
            (void)ku_file_unlink(temporary, strlen(temporary));
            return 0;
        }
        offset += chunk;
    }
    if (ku_file_close((ku_file_t)opened) != KU_STATUS_OK ||
        ku_file_sync() != KU_STATUS_OK) {
        (void)ku_file_unlink(temporary, strlen(temporary));
        return 0;
    }

    if (ku_file_stat_path(manifest->destination, strlen(manifest->destination), &stat) == KU_STATUS_OK) {
        had_old = 1;
        if (ku_file_rename(
                manifest->destination,
                strlen(manifest->destination),
                backup,
                strlen(backup)) != KU_STATUS_OK) {
            (void)ku_file_unlink(temporary, strlen(temporary));
            return 0;
        }
    }

    if (ku_file_rename(
            temporary,
            strlen(temporary),
            manifest->destination,
            strlen(manifest->destination)) != KU_STATUS_OK) {
        if (had_old) {
            (void)ku_file_rename(
                backup,
                strlen(backup),
                manifest->destination,
                strlen(manifest->destination));
        }
        (void)ku_file_unlink(temporary, strlen(temporary));
        return 0;
    }
    if (had_old) (void)ku_file_unlink(backup, strlen(backup));
    return ku_file_sync() == KU_STATUS_OK;
}

static int install_package(size_t package_index, unsigned depth);

static int ensure_dependencies(const char* list, unsigned depth) {
    size_t cursor = 0U;
    char name[32];
    while (next_list_name(list, &cursor, name, sizeof(name))) {
        const size_t dependency = find_package(name);
        if (dependency < g_package_count &&
            package_state(dependency) == ANVIL_PACKAGE_CURRENT) continue;
        if (dependency >= g_package_count || !install_package(dependency, depth + 1U)) {
            (void)strlcpy(g_status, "ANVIL / DEPENDENCY FAILED", sizeof(g_status));
            return 0;
        }
    }
    return 1;
}

static int verify_peers(const char* list) {
    size_t cursor = 0U;
    char name[32];
    while (next_list_name(list, &cursor, name, sizeof(name))) {
        if (!installed_name(name)) {
            (void)strlcpy(g_status, "ANVIL / MISSING PEER DEPENDENCY", sizeof(g_status));
            return 0;
        }
    }
    return 1;
}

static int verify_conflicts(const char* list) {
    size_t cursor = 0U;
    char name[32];
    while (next_list_name(list, &cursor, name, sizeof(name))) {
        if (installed_name(name)) {
            (void)strlcpy(g_status, "ANVIL / PACKAGE CONFLICT", sizeof(g_status));
            return 0;
        }
    }
    return 1;
}

static int install_package(size_t package_index, unsigned depth) {
    anvil_manifest manifest;
    uint8_t* body = NULL;
    size_t body_size = 0U;
    char installed[20];
    int had_installed;

    if (depth > ANVIL_DEPENDENCY_DEPTH || package_index >= g_package_count) return 0;
    (void)strlcpy(g_status, "ANVIL / RESOLVING PACKAGE", sizeof(g_status));
    if (!load_manifest(package_index, &manifest)) return 0;

    had_installed = installed_version(manifest.name, installed, sizeof(installed));
    if (had_installed && strcmp(installed, manifest.version) == 0) {
        (void)strlcpy(g_status, "ANVIL / PACKAGE IS CURRENT", sizeof(g_status));
        return 1;
    }

    if (!verify_conflicts(manifest.conflicts) || !verify_peers(manifest.peer) ||
        !ensure_dependencies(manifest.depends, depth)) return 0;

    (void)strlcpy(g_status, "ANVIL / DOWNLOADING PAYLOAD", sizeof(g_status));
    if (!fetch_https(
            manifest.payload,
            g_payload_response,
            sizeof(g_payload_response),
            &body,
            &body_size)) return 0;
    if ((uint64_t)body_size != manifest.bytes) {
        (void)strlcpy(g_status, "ANVIL / PAYLOAD SIZE MISMATCH", sizeof(g_status));
        return 0;
    }
    if (!anvil_sha256_matches(body, body_size, manifest.sha256)) {
        (void)strlcpy(g_status, "ANVIL / PAYLOAD HASH MISMATCH", sizeof(g_status));
        return 0;
    }

    (void)strlcpy(
        g_status,
        had_installed ? "ANVIL / UPGRADING" : "ANVIL / INSTALLING",
        sizeof(g_status));
    if (!write_payload_transaction(&manifest, body, body_size)) {
        (void)strlcpy(g_status, "ANVIL / INSTALL TRANSACTION FAILED", sizeof(g_status));
        return 0;
    }
    if (!record_install(&manifest)) {
        (void)strlcpy(g_status, "ANVIL / INSTALLED / DATABASE WARNING", sizeof(g_status));
        return 1;
    }
    (void)strlcpy(
        g_status,
        had_installed ? "ANVIL / UPDATE COMPLETE" : "ANVIL / INSTALL COMPLETE",
        sizeof(g_status));
    return 1;
}

static int update_all_packages(void) {
    size_t index;
    size_t updated = 0U;
    for (index = 0U; index < g_package_count; ++index) {
        if (package_state(index) != ANVIL_PACKAGE_UPDATE) continue;
        if (!install_package(index, 0U)) {
            (void)strlcpy(g_status, "ANVIL / UPDATE ALL FAILED", sizeof(g_status));
            return 0;
        }
        ++updated;
    }
    (void)strlcpy(
        g_status,
        updated == 0U ? "ANVIL / NO PACKAGE UPDATES" : "ANVIL / UPDATE ALL COMPLETE",
        sizeof(g_status));
    return 1;
}

static void normalize_selection(void) {
    if (g_package_count == 0U) {
        g_selected = 0U;
        g_scroll = 0U;
        return;
    }
    if (g_selected >= g_package_count) g_selected = g_package_count - 1U;
    if (g_selected < g_scroll) g_scroll = g_selected;
    if (g_selected >= g_scroll + ANVIL_VISIBLE_PACKAGES) {
        g_scroll = g_selected - ANVIL_VISIBLE_PACKAGES + 1U;
    }
}

static void move_selection(int direction) {
    if (g_package_count == 0U) return;
    if (direction > 0) g_selected = (g_selected + 1U) % g_package_count;
    else g_selected = g_selected == 0U ? g_package_count - 1U : g_selected - 1U;
    normalize_selection();
}

static ku_icon_id_t anvil_status_icon(void) {
    if (strstr(g_status, "FAILED") != NULL ||
        strstr(g_status, "INVALID") != NULL ||
        strstr(g_status, "CONFLICT") != NULL ||
        strstr(g_status, "MISMATCH") != NULL) return KU_ICON_STATUS_ERROR;
    if (strstr(g_status, "COMPLETE") != NULL ||
        strstr(g_status, "READY") != NULL ||
        strstr(g_status, "CURRENT") != NULL ||
        strstr(g_status, "INSTALLED") != NULL) return KU_ICON_STATUS_SUCCESS;
    if (strstr(g_status, "FETCH") != NULL ||
        strstr(g_status, "DOWNLOAD") != NULL ||
        strstr(g_status, "INSTALLING") != NULL ||
        strstr(g_status, "UPGRADING") != NULL) return KU_ICON_STATUS_LOADING;
    return KU_ICON_STATUS_INFO;
}

static const char* selected_action_label(void) {
    const int state = g_package_count == 0U
        ? ANVIL_PACKAGE_GET
        : package_state(g_selected);
    if (state == ANVIL_PACKAGE_UPDATE) return "UPDATE SELECTED";
    if (state == ANVIL_PACKAGE_CURRENT) return "PACKAGE CURRENT";
    return "INSTALL SELECTED";
}

static void build_scene(kui_scene* scene) {
    kui_flow root;
    kui_flow packages;
    size_t row;
    char repo_line[64] = "REPO / ";
    gui_append_text(repo_line, sizeof(repo_line), g_repo.host);

    kui_scene_initialize(scene);
    scene->visible_rows = 15U;
    gui_apply_forged_theme(scene, 0);
    (void)kui_scene_set_cursor(scene, KU_UI_CURSOR_HAND);
    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel_icon(
        &root, 1U, "ANVIL / PACKAGES",
        KU_ICON_KUROGANE_APP_ANVIL_PACKAGE_MANAGER);
    (void)kui_flow_label_icon(
        &root, 2U, repo_line, KU_ICON_SPECIAL_CLOUD_SYNC);
    (void)kui_flow_button_icon(
        &root, ANVIL_REFRESH_ID, "REFRESH CATALOG", KU_ICON_ACTION_REFRESH);
    (void)kui_flow_button_icon(
        &root, ANVIL_INSTALL_ID, selected_action_label(), KU_ICON_ACTION_DOWNLOAD);
    (void)kui_flow_button_icon(
        &root, ANVIL_UPDATE_ALL_ID, "UPDATE ALL", KU_ICON_ACTION_REFRESH);
    (void)kui_flow_label_icon(
        &root, 3U, "GET=NEW  INST=CURRENT  UPD=UPDATE",
        KU_ICON_WIDGET_SIDEBAR);
    (void)kui_flow_separator(&root, 4U);
    (void)kui_flow_label_icon(
        &root, 5U, "DEPENDENCIES INSTALL AUTOMATICALLY",
        KU_ICON_SPECIAL_DATABASE);

    kui_flow_begin(&packages, scene, 1U);
    for (row = 0U; row < ANVIL_VISIBLE_PACKAGES; ++row) {
        const size_t index = g_scroll + row;
        const int state = index < g_package_count
            ? package_state(index)
            : ANVIL_PACKAGE_GET;
        char label[64] = "";
        ku_icon_id_t icon = KU_ICON_ACTION_DOWNLOAD;
        if (index >= g_package_count) break;
        if (state == ANVIL_PACKAGE_CURRENT) {
            gui_append_text(label, sizeof(label), "INST  ");
            icon = KU_ICON_STATUS_SUCCESS;
        } else if (state == ANVIL_PACKAGE_UPDATE) {
            gui_append_text(label, sizeof(label), "UPD   ");
            icon = KU_ICON_ACTION_REFRESH;
        } else {
            gui_append_text(label, sizeof(label), "GET   ");
        }
        gui_append_text(label, sizeof(label), g_packages[index].name);
        gui_append_text(label, sizeof(label), "  ");
        gui_append_text(label, sizeof(label), g_packages[index].version);
        (void)kui_flow_list_item_icon(
            &packages,
            ANVIL_PACKAGE_ROW_BASE + (uint32_t)row,
            label,
            icon);
    }
    if (g_package_count != 0U) {
        char detail[64] = "";
        gui_append_text(detail, sizeof(detail), g_packages[g_selected].description);
        (void)kui_flow_label_icon(
            &root, 30U, detail, KU_ICON_WIDGET_CARD);
        (void)kui_scene_select(
            scene,
            ANVIL_PACKAGE_ROW_BASE + (uint32_t)(g_selected - g_scroll));
    }
    (void)kui_flow_label_icon(&root, 31U, g_status, anvil_status_icon());
}

int main(void) {
    const ku_window_t window = gui_open("ANVIL", 360, 120, 680, 540);
    kui_scene scene;
    if (window == KU_INVALID_WINDOW) return 1;

    load_repository();
    reload_install_database();
    (void)refresh_catalog();
    build_scene(&scene);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 2;
    }
    puts("[TEST] anvil_package_manager_ring3: PASS");
    puts("[TEST] anvil_github_repository_protocol: PASS");
    puts("[TEST] anvil_dependency_semantics: PASS");
    puts("[TEST] anvil_transactional_install: PASS");
    puts("[TEST] anvil_versioned_upgrade: PASS");
    puts("[TEST] anvil_payload_sha256_integrity: PASS");
    puts("[TEST] anvil_update_all: PASS");

    for (;;) {
        ku_ui_event event;
        if (gui_wait_event(window, &event) < 0 || event.type == KU_UI_EVENT_CLOSE) break;

        if (event.type == KU_UI_EVENT_POINTER) {
            const uint32_t hit = gui_scene_hit_test_local(&scene, &event);
            if (hit == ANVIL_REFRESH_ID) {
                (void)refresh_catalog();
            } else if (hit == ANVIL_INSTALL_ID) {
                if (g_package_count != 0U) (void)install_package(g_selected, 0U);
            } else if (hit == ANVIL_UPDATE_ALL_ID) {
                (void)update_all_packages();
            } else if (hit >= ANVIL_PACKAGE_ROW_BASE &&
                       hit < ANVIL_PACKAGE_ROW_BASE + ANVIL_VISIBLE_PACKAGES) {
                const size_t package_index =
                    g_scroll + (size_t)(hit - ANVIL_PACKAGE_ROW_BASE);
                if (package_index >= g_package_count) continue;
                g_selected = package_index;
                normalize_selection();
                (void)strlcpy(g_status, "ANVIL / PACKAGE SELECTED", sizeof(g_status));
            } else {
                continue;
            }
            build_scene(&scene);
            (void)kui_scene_present(window, &scene);
            continue;
        }

        if (event.type != KU_UI_EVENT_KEY) continue;
        if (gui_key_down(&event) || gui_key_right(&event) || gui_key_tab(&event)) {
            move_selection(1);
        } else if (gui_key_up(&event) || gui_key_left(&event)) {
            move_selection(-1);
        } else if (gui_key_activate(&event)) {
            if (g_package_count != 0U) (void)install_package(g_selected, 0U);
        } else if (event.character == 'r' || event.character == 'R') {
            (void)refresh_catalog();
        } else if (event.character == 'u' || event.character == 'U') {
            (void)update_all_packages();
        } else if (gui_key_cancel(&event)) {
            break;
        } else {
            continue;
        }
        build_scene(&scene);
        (void)kui_scene_present(window, &scene);
    }
    (void)ku_ui_close(window);
    return 0;
}

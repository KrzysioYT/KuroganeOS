#include "../../runtime/user.h"

#include <kurogane/account.h>
#include "dev_profile.h"

#define ACCOUNTD_MAX_CLIENTS 8U
#define ACCOUNT_CONFIG_CAPACITY 384U

typedef struct accountd_client {
    ku_service_connection_t connection;
    uint64_t pid;
    int active;
} accountd_client;

typedef struct accountd_profile {
    uint64_t account_id;
    uint32_t flags;
    char username[KU_ACCOUNT_USERNAME_CAPACITY];
    char locale[KU_ACCOUNT_LOCALE_CAPACITY];
    int available;
} accountd_profile;

static accountd_client clients[ACCOUNTD_MAX_CLIENTS];
static accountd_profile profile;

static void clear_bytes(void* destination, size_t size) {
    uint8_t* bytes = (uint8_t*)destination;
    size_t index;
    for (index = 0U; index < size; ++index) bytes[index] = 0U;
}

static void copy_text(char* destination, size_t capacity, const char* source) {
    size_t index = 0U;
    if (destination == (char*)0 || source == (const char*)0 || capacity == 0U) return;
    while (index + 1U < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index++] = '\0';
    while (index < capacity) destination[index++] = '\0';
}

static int text_equal(const char* left, const char* right) {
    size_t index = 0U;
    if (left == (const char*)0 || right == (const char*)0) return 0;
    while (left[index] != '\0' && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

static int bounded_text_valid(const char* text, size_t capacity, int allow_empty) {
    size_t index;
    if (text == (const char*)0 || capacity == 0U) return 0;
    if (!allow_empty && text[0] == '\0') return 0;
    for (index = 0U; index < capacity; ++index) {
        const unsigned char value = (unsigned char)text[index];
        if (value == 0U) return 1;
        if (value < 0x20U || value > 0x7EU) return 0;
    }
    return 0;
}

static int read_small_file(const char* path, char* output, size_t capacity) {
    const ku_result_t opened = ku_open(path, u_strlen(path), KU_OPEN_READ);
    ku_result_t result;
    ku_handle_t handle;
    if (output == (char*)0 || capacity < 2U || opened <= 0) return 0;
    handle = (ku_handle_t)opened;
    result = ku_read(handle, output, capacity - 1U);
    (void)ku_close(handle);
    if (result < 0) return 0;
    output[(size_t)result] = '\0';
    return 1;
}

static void load_profile(void) {
    char user_config[ACCOUNT_CONFIG_CAPACITY];
    char locale_config[64];
    char locale[KU_DEV_PROFILE_LOCALE_CAPACITY];
    struct ku_dev_profile_data parsed;
    const int installed = read_small_file("/etc/user.cfg", user_config, sizeof(user_config));
    const int locale_read = read_small_file("/etc/locale.cfg", locale_config, sizeof(locale_config));
    int locale_valid = 0;

    clear_bytes(&profile, sizeof(profile));
    clear_bytes(&parsed, sizeof(parsed));
    clear_bytes(locale, sizeof(locale));
    profile.account_id = UINT64_C(1);

    if (!installed) {
        profile.flags = KU_ACCOUNT_FLAG_LIVE | KU_ACCOUNT_FLAG_PROFILE_VALID;
        copy_text(profile.username, sizeof(profile.username), "developer");
        copy_text(profile.locale, sizeof(profile.locale), "en-US");
        profile.available = 1;
        return;
    }

    if (locale_read) locale_valid = ku_dev_profile_parse_locale(locale_config, locale);
    if (!locale_valid || !ku_dev_profile_parse_user_config(user_config, &parsed)) {
        profile.flags = KU_ACCOUNT_FLAG_INSTALLED;
        profile.available = 0;
        return;
    }

    profile.flags = KU_ACCOUNT_FLAG_INSTALLED | KU_ACCOUNT_FLAG_PROFILE_VALID;
    if (parsed.password_required) profile.flags |= KU_ACCOUNT_FLAG_PASSWORD_REQUIRED;
    copy_text(profile.username, sizeof(profile.username), parsed.username);
    copy_text(profile.locale, sizeof(profile.locale), locale);
    profile.available = 1;
}

static ku_status_t send_response(
    ku_service_connection_t connection,
    ku_status_t status,
    const accountd_profile* source) {
    ku_account_response response;
    clear_bytes(&response, sizeof(response));
    response.structure_size = sizeof(response);
    response.status = status;
    if (status == KU_STATUS_OK && source != (const accountd_profile*)0) {
        response.account_id = source->account_id;
        response.flags = source->flags;
        copy_text(response.username, sizeof(response.username), source->username);
        copy_text(response.locale, sizeof(response.locale), source->locale);
    }
    return ku_service_send(connection, &response, sizeof(response));
}

static void handle_request(accountd_client* client, const ku_service_message* message) {
    const ku_account_request* request;
    int match;
    if (client == (accountd_client*)0 || message == (const ku_service_message*)0) return;
    if (message->data_size != sizeof(ku_account_request)) {
        (void)send_response(client->connection, KU_STATUS_CORRUPT_DATA, 0);
        return;
    }
    request = (const ku_account_request*)(const void*)message->data;
    if (request->structure_size != sizeof(*request) || request->reserved != 0U) {
        (void)send_response(client->connection, KU_STATUS_INVALID_ARGUMENT, 0);
        return;
    }
    if (client->pid == 0U) client->pid = message->sender_pid;
    else if (client->pid != message->sender_pid) {
        (void)send_response(client->connection, KU_STATUS_ACCESS_DENIED, 0);
        return;
    }
    if (!profile.available) {
        (void)send_response(client->connection, KU_STATUS_CORRUPT_DATA, 0);
        return;
    }

    if (request->operation == KU_ACCOUNT_GET_CURRENT) {
        if (request->username[0] != '\0') {
            (void)send_response(client->connection, KU_STATUS_INVALID_ARGUMENT, 0);
            return;
        }
        (void)send_response(client->connection, KU_STATUS_OK, &profile);
        return;
    }
    if (request->operation == KU_ACCOUNT_LOOKUP) {
        if (!bounded_text_valid(request->username, sizeof(request->username), 0)) {
            (void)send_response(client->connection, KU_STATUS_INVALID_ARGUMENT, 0);
            return;
        }
        match = text_equal(request->username, profile.username);
        (void)send_response(
            client->connection, match ? KU_STATUS_OK : KU_STATUS_NOT_FOUND,
            match ? &profile : (const accountd_profile*)0);
        return;
    }
    (void)send_response(client->connection, KU_STATUS_NOT_SUPPORTED, 0);
}

static void accept_clients(ku_service_endpoint_t endpoint) {
    for (;;) {
        const ku_result_t accepted = ku_service_accept(endpoint);
        size_t index;
        if (accepted == KU_STATUS_WOULD_BLOCK) return;
        if (accepted <= 0) return;
        for (index = 0U; index < ACCOUNTD_MAX_CLIENTS; ++index) {
            if (!clients[index].active) {
                clients[index].connection = (ku_service_connection_t)accepted;
                clients[index].pid = 0U;
                clients[index].active = 1;
                break;
            }
        }
        if (index == ACCOUNTD_MAX_CLIENTS) {
            (void)ku_service_close((ku_service_connection_t)accepted);
            return;
        }
    }
}

static void service_clients(void) {
    size_t index;
    for (index = 0U; index < ACCOUNTD_MAX_CLIENTS; ++index) {
        accountd_client* client = &clients[index];
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
        KU_ACCOUNT_SERVICE_NAME, KU_ACCOUNT_SERVICE_NAME_SIZE);
    if (endpoint <= 0) {
        (void)u_puts("accountd: service registration failed\n");
        (void)u_puts("[TEST] account_service: FAIL\n");
        ku_exit(1);
    }
    load_profile();
    (void)u_puts("accountd: account.v1 online\n");
    (void)u_puts("[TEST] account_service: PASS\n");
    for (;;) {
        accept_clients((ku_service_endpoint_t)endpoint);
        service_clients();
        (void)ku_sleep(1U);
    }
}

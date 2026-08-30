#include "../common.h"
#include "../../../common/version.h"
#include "../../../common/dev_profile.h"

#include <kurogane/account.h>
#include <kurogane/session.h>

#define LOGIN_USERNAME_CAPACITY KU_DEV_PROFILE_USERNAME_CAPACITY
#define LOGIN_PASSWORD_CAPACITY 48U
#define LOGIN_CONFIG_CAPACITY 384U
#define LOGIN_SERVICE_ATTEMPTS 200U

typedef struct login_profile {
    char username[LOGIN_USERNAME_CAPACITY];
    char locale[KU_DEV_PROFILE_LOCALE_CAPACITY];
    uint64_t password_hash;
    int password_required;
    int installed_profile;
    int profile_valid;
} login_profile;

static int read_small_file(const char* path, char* output, size_t capacity) {
    const ku_result_t opened = ku_open(path, strlen(path), KU_OPEN_READ);
    if (opened <= 0 || output == NULL || capacity < 2U) return 0;
    {
        const ku_handle_t handle = (ku_handle_t)opened;
        const ku_result_t result = ku_read(handle, output, capacity - 1U);
        (void)ku_close(handle);
        if (result < 0) return 0;
        output[(size_t)result] = '\0';
        return 1;
    }
}

static void load_profile(login_profile* profile) {
    char config[LOGIN_CONFIG_CAPACITY];
    char locale[KU_DEV_PROFILE_LOCALE_CAPACITY];
    struct ku_dev_profile_data parsed = {{0}, 0U, 0};
    int locale_valid = 0;

    if (profile == NULL) return;
    memset(profile, 0, sizeof(*profile));
    (void)strlcpy(profile->username, "developer", sizeof(profile->username));
    (void)strlcpy(profile->locale, "en-US", sizeof(profile->locale));
    profile->profile_valid = 1;

    if (read_small_file("/etc/locale.cfg", config, sizeof(config))) {
        locale_valid = ku_dev_profile_parse_locale(config, locale);
        if (locale_valid) {
            (void)strlcpy(profile->locale, locale, sizeof(profile->locale));
        }
    }

    if (!read_small_file("/etc/user.cfg", config, sizeof(config))) return;

    profile->installed_profile = 1;
    if (!locale_valid || !ku_dev_profile_parse_user_config(config, &parsed)) {
        /*
         * Fail closed. A truncated/tampered installed profile must never turn
         * into the zero-initialized no-password path.
         */
        profile->profile_valid = 0;
        profile->password_required = 1;
        profile->password_hash = 0U;
        return;
    }

    (void)strlcpy(profile->username, parsed.username, sizeof(profile->username));
    profile->password_required = parsed.password_required;
    profile->password_hash = parsed.password_hash;
}

static int is_polish(const login_profile* profile) {
    return profile != NULL && profile->locale[0] == 'p' && profile->locale[1] == 'l';
}

static void build_scene(
    kui_scene* scene,
    const login_profile* profile,
    const char* password,
    const char* error) {
    kui_flow root;
    kui_flow session;
    char account_line[64] = "ACCOUNT / ";
    char masked[LOGIN_PASSWORD_CAPACITY];
    size_t index;
    const int polish = is_polish(profile);

    kui_scene_initialize(scene);
    scene->visible_rows = 12U;
    kui_scene_set_palette(
        scene,
        UINT32_C(0x0B0C0F),
        UINT32_C(0xEEF0F3),
        UINT32_C(0xE0162B));

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U,
        polish ? "LOGOWANIE KUROGANEOS" : "KUROGANEOS LOGIN");
    (void)kui_flow_label(&root, 2U, KUROGANE_PRODUCT_STRING " / DEV BETA");
    (void)strlcpy(account_line + strlen(account_line), profile->username,
                  sizeof(account_line) - strlen(account_line));
    (void)kui_flow_label(&root, 3U, account_line);
    (void)kui_flow_separator(&root, 4U);

    kui_flow_begin(&session, scene, 1U);
    if (profile->installed_profile && !profile->profile_valid) {
        (void)kui_flow_label(&session, 9U,
            polish ? "PROFIL KONTA JEST USZKODZONY"
                   : "ACCOUNT PROFILE IS INVALID");
        (void)kui_flow_label(&session, 10U,
            polish ? "LOGOWANIE ZABLOKOWANE - WYMAGANE ODZYSKIWANIE"
                   : "LOGIN BLOCKED - RECOVERY REQUIRED");
        (void)kui_flow_label(&session, 11U,
            polish ? "NIE MOZNA BEZPIECZNIE ZWERYFIKOWAC KONTA"
                   : "ACCOUNT CREDENTIALS CANNOT BE VERIFIED SAFELY");
        return;
    }

    if (!profile->installed_profile) {
        (void)kui_flow_label(&session, 9U,
            polish ? "SESJA LIVE / TYLKO DO ODCZYTU"
                   : "LIVE SESSION / READ-ONLY SYSTEM ROOT");
    }
    if (profile->password_required) {
        const size_t length = strlen(password);
        const size_t count = length < sizeof(masked) - 1U
            ? length : sizeof(masked) - 1U;
        for (index = 0U; index < count; ++index) masked[index] = '*';
        masked[count] = '\0';
        (void)kui_flow_label(&session, 10U,
            polish ? "HASLO" : "PASSWORD");
        (void)kui_flow_input(&session, 11U,
            masked[0] != '\0' ? masked : "_");
        (void)kui_flow_label(&session, 12U,
            polish ? "WPISZ HASLO I NACISNIJ ENTER"
                   : "TYPE PASSWORD AND PRESS ENTER");
    } else {
        (void)kui_flow_button(&session, 10U,
            polish ? "WEJDZ DO RED FLUX" : "ENTER RED FLUX DESKTOP");
        (void)kui_flow_label(&session, 11U,
            polish ? "ENTER LUB KLIKNIJ, ABY OTWORZYC SESJE"
                   : "ENTER OR CLICK TO START THE SESSION");
    }
    if (error != NULL && error[0] != '\0') {
        (void)kui_flow_label(&session, 13U, error);
    }
    (void)kui_scene_select(scene, 10U);
}

static ku_result_t connect_service(const char* name, size_t name_size) {
    uint32_t attempt;
    ku_result_t result = KU_STATUS_NOT_FOUND;
    for (attempt = 0U; attempt < LOGIN_SERVICE_ATTEMPTS; ++attempt) {
        result = ku_service_connect(name, name_size);
        if (result > 0) return result;
        if (result != KU_STATUS_NOT_FOUND && result != KU_STATUS_WOULD_BLOCK) {
            return result;
        }
        (void)kuro_sleep(1U);
    }
    return result;
}

static ku_status_t account_transact(
    ku_service_connection_t connection,
    const ku_account_request* request,
    ku_account_response* response) {
    uint32_t attempt;
    ku_status_t status = ku_service_send(connection, request, sizeof(*request));
    if (status != KU_STATUS_OK) return status;
    for (attempt = 0U; attempt < LOGIN_SERVICE_ATTEMPTS; ++attempt) {
        ku_service_message message;
        status = ku_service_receive(connection, &message);
        if (status == KU_STATUS_WOULD_BLOCK) {
            (void)kuro_sleep(1U);
            continue;
        }
        if (status != KU_STATUS_OK) return status;
        if (message.data_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        *response = *(const ku_account_response*)(const void*)message.data;
        if (response->structure_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        return (ku_status_t)response->status;
    }
    return KU_STATUS_WOULD_BLOCK;
}

static ku_status_t session_transact(
    ku_service_connection_t connection,
    const ku_session_request* request,
    ku_session_response* response) {
    uint32_t attempt;
    ku_status_t status = ku_service_send(connection, request, sizeof(*request));
    if (status != KU_STATUS_OK) return status;
    for (attempt = 0U; attempt < LOGIN_SERVICE_ATTEMPTS; ++attempt) {
        ku_service_message message;
        status = ku_service_receive(connection, &message);
        if (status == KU_STATUS_WOULD_BLOCK) {
            (void)kuro_sleep(1U);
            continue;
        }
        if (status != KU_STATUS_OK) return status;
        if (message.data_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        *response = *(const ku_session_response*)(const void*)message.data;
        if (response->structure_size != sizeof(*response)) return KU_STATUS_CORRUPT_DATA;
        return (ku_status_t)response->status;
    }
    return KU_STATUS_WOULD_BLOCK;
}

static int current_account(const login_profile* profile, uint64_t* account_id) {
    ku_result_t connected;
    ku_service_connection_t connection;
    ku_account_request request;
    ku_account_response response;
    ku_status_t status;

    if (profile == NULL || account_id == NULL) return 0;
    *account_id = 0U;
    connected = connect_service(KU_ACCOUNT_SERVICE_NAME, KU_ACCOUNT_SERVICE_NAME_SIZE);
    if (connected <= 0) return 0;
    connection = (ku_service_connection_t)connected;
    memset(&request, 0, sizeof(request));
    memset(&response, 0, sizeof(response));
    request.structure_size = sizeof(request);
    request.operation = KU_ACCOUNT_GET_CURRENT;
    status = account_transact(connection, &request, &response);
    (void)ku_service_close(connection);
    if (status != KU_STATUS_OK || response.account_id == 0U ||
        (response.flags & KU_ACCOUNT_FLAG_PROFILE_VALID) == 0U ||
        strcmp(response.username, profile->username) != 0) {
        return 0;
    }
    *account_id = response.account_id;
    return 1;
}

static int session_request(
    ku_service_connection_t connection,
    uint32_t operation,
    uint64_t session_id,
    uint64_t account_id,
    uint64_t process_id,
    ku_session_response* response) {
    ku_session_request request;
    memset(&request, 0, sizeof(request));
    memset(response, 0, sizeof(*response));
    request.structure_size = sizeof(request);
    request.operation = operation;
    request.session_id = session_id;
    request.account_id = account_id;
    request.process_id = process_id;
    return session_transact(connection, &request, response) == KU_STATUS_OK;
}

static int response_has_application(
    const ku_session_response* response,
    uint64_t process_id) {
    uint32_t index;
    if (response == NULL) return 0;
    for (index = 0U; index < response->application_count &&
         index < KU_SESSION_MAX_APPLICATIONS; ++index) {
        if (response->applications[index] == process_id) return 1;
    }
    return 0;
}

static int wait_for_desktop(uint64_t pid) {
    for (;;) {
        int32_t status = 0;
        const ku_status_t result = ku_process_wait(pid, &status);
        if (result == KU_STATUS_OK) return status;
        if (result != KU_STATUS_WOULD_BLOCK) return 3;
        (void)kuro_sleep(2U);
    }
}

static int start_session(ku_window_t window, const login_profile* profile) {
    const char launcher[] = "/gui/launcher";
    ku_result_t connected;
    ku_service_connection_t connection;
    ku_session_response response;
    uint64_t account_id = 0U;
    uint64_t session_id = 0U;
    uint64_t launcher_pid = 0U;
    int desktop_status = 4;

    if (!current_account(profile, &account_id)) return 5;
    connected = connect_service(KU_SESSION_SERVICE_NAME, KU_SESSION_SERVICE_NAME_SIZE);
    if (connected <= 0) return 6;
    connection = (ku_service_connection_t)connected;

    if (!session_request(connection, KU_SESSION_CREATE, 0U, account_id, 0U, &response) ||
        response.session_id == 0U || response.account_id != account_id ||
        response.owner_pid != ku_process_id() || response.state != KU_SESSION_STATE_ACTIVE) {
        (void)ku_service_close(connection);
        return 7;
    }
    session_id = response.session_id;

    (void)ku_ui_close(window);
    {
        const ku_result_t spawned = ku_process_spawn(launcher, sizeof(launcher) - 1U);
        if (spawned <= 0) goto cleanup;
        launcher_pid = (uint64_t)spawned;
    }

    if (!session_request(
            connection, KU_SESSION_SET_HOME, session_id, 0U, launcher_pid, &response)) {
        goto cleanup;
    }
    if (!session_request(
            connection, KU_SESSION_ATTACH_APPLICATION, session_id, 0U,
            launcher_pid, &response)) {
        goto cleanup;
    }
    if (!session_request(connection, KU_SESSION_QUERY, session_id, 0U, 0U, &response) ||
        response.account_id != account_id || response.owner_pid != ku_process_id() ||
        response.home_pid != launcher_pid || response.state != KU_SESSION_STATE_ACTIVE ||
        !response_has_application(&response, launcher_pid)) {
        goto cleanup;
    }

    puts("[TEST] red_flux_owned_session: PASS");
    puts("[TEST] red_flux_login_to_desktop: PASS");
    desktop_status = wait_for_desktop(launcher_pid);

cleanup:
    if (launcher_pid != 0U) {
        (void)session_request(
            connection, KU_SESSION_DETACH_APPLICATION, session_id, 0U,
            launcher_pid, &response);
    }
    (void)session_request(
        connection, KU_SESSION_TERMINATE, session_id, 0U, 0U, &response);
    (void)ku_service_close(connection);
    return desktop_status;
}

int main(void) {
    login_profile profile;
    char password[LOGIN_PASSWORD_CAPACITY] = {0};
    size_t password_length = 0U;
    const char* error = NULL;
    ku_window_t window;
    kui_scene scene;

    load_profile(&profile);
    window = gui_open("KUROGANE LOGIN", 280, 245, 520, 290);
    if (window == KU_INVALID_WINDOW) return 1;

    build_scene(&scene, &profile, password, error);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 2;
    }

    puts("[TEST] red_flux_login_surface: PASS");
    if (profile.installed_profile && !profile.profile_valid) {
        puts("[TEST] installed_account_profile_invalid_blocked: PASS");
    } else {
        puts(profile.installed_profile
            ? "[TEST] installed_account_profile: PASS"
            : "[TEST] live_login_profile: PASS");
    }

    for (;;) {
        ku_ui_event event;
        const int available = gui_wait_event(window, &event);
        if (available < 0 || event.type == KU_UI_EVENT_CLOSE) {
            (void)ku_ui_close(window);
            return 0;
        }

        if (profile.installed_profile && !profile.profile_valid) {
            /* Invalid persistent credentials are a hard authentication gate. */
            continue;
        }

        if (!profile.password_required &&
            event.type == KU_UI_EVENT_POINTER &&
            (event.buttons & UINT32_C(1)) != 0U) {
            return start_session(window, &profile);
        }
        if (event.type != KU_UI_EVENT_KEY) continue;

        if (!profile.password_required) {
            if (gui_key_activate(&event)) return start_session(window, &profile);
            continue;
        }

        if (event.key == KU_UI_KEY_BACKSPACE) {
            if (password_length != 0U) password[--password_length] = '\0';
            error = NULL;
        } else if (gui_key_cancel(&event)) {
            password_length = 0U;
            password[0] = '\0';
            error = NULL;
        } else if (gui_key_activate(&event)) {
            if (ku_dev_credential_verify(
                    profile.username, password, profile.password_hash)) {
                puts("[TEST] installed_login_password: PASS");
                return start_session(window, &profile);
            }
            puts("[TEST] installed_login_bad_password_rejected: PASS");
            password_length = 0U;
            password[0] = '\0';
            error = is_polish(&profile)
                ? "NIEPRAWIDLOWE HASLO"
                : "INCORRECT PASSWORD";
        } else if (event.character >= 0x20U && event.character <= 0x7EU &&
                   password_length + 1U < sizeof(password)) {
            password[password_length++] = (char)event.character;
            password[password_length] = '\0';
            error = NULL;
        } else {
            continue;
        }

        build_scene(&scene, &profile, password, error);
        (void)kui_scene_present(window, &scene);
    }
}

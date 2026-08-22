#include "../common.h"
#include "../../../common/version.h"

#define LOGIN_USERNAME_CAPACITY 24U
#define LOGIN_PASSWORD_CAPACITY 48U
#define LOGIN_CONFIG_CAPACITY 384U
#define LOGIN_ACTION_ID 22U

typedef struct login_profile {
    char username[LOGIN_USERNAME_CAPACITY];
    char locale[16];
    uint64_t password_hash;
    int password_required;
    int installed_profile;
} login_profile;

static uint64_t credential_hash(const char* username, const char* password) {
    uint64_t hash = UINT64_C(1469598103934665603);
    /* Keep the existing credential domain stable across the visual migration. */
    const char domain[] = "KuroganeOS-3.3-dev:";
    size_t index;
    for (index = 0U; domain[index] != '\0'; ++index) {
        hash ^= (uint8_t)domain[index];
        hash *= UINT64_C(1099511628211);
    }
    {
        const char* fields[3] = {username, ":", password};
        size_t field;
        for (field = 0U; field < 3U; ++field) {
            const char* value = fields[field];
            if (value == NULL) continue;
            for (index = 0U; value[index] != '\0'; ++index) {
                hash ^= (uint8_t)value[index];
                hash *= UINT64_C(1099511628211);
            }
        }
    }
    return hash;
}

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

static int copy_config_value(
    const char* config,
    const char* key,
    char* output,
    size_t capacity) {
    size_t key_length;
    size_t start;
    if (config == NULL || key == NULL || output == NULL || capacity == 0U) return 0;
    key_length = strlen(key);
    for (start = 0U; config[start] != '\0'; ++start) {
        size_t index = 0U;
        if (start != 0U && config[start - 1U] != '\n') continue;
        while (index < key_length && config[start + index] == key[index]) ++index;
        if (index != key_length || config[start + index] != '=') continue;
        {
            size_t source = start + key_length + 1U;
            size_t written = 0U;
            while (config[source] != '\0' && config[source] != '\n' &&
                   written + 1U < capacity) {
                output[written++] = config[source++];
            }
            output[written] = '\0';
            return 1;
        }
    }
    output[0] = '\0';
    return 0;
}

static int parse_hex64(const char* value, uint64_t* output) {
    uint64_t result = 0U;
    size_t index;
    if (value == NULL || output == NULL || strlen(value) != 16U) return 0;
    for (index = 0U; index < 16U; ++index) {
        uint64_t digit;
        const char ch = value[index];
        if (ch >= '0' && ch <= '9') digit = (uint64_t)(ch - '0');
        else if (ch >= 'A' && ch <= 'F') digit = (uint64_t)(ch - 'A' + 10);
        else if (ch >= 'a' && ch <= 'f') digit = (uint64_t)(ch - 'a' + 10);
        else return 0;
        result = (result << 4U) | digit;
    }
    *output = result;
    return 1;
}

static void load_profile(login_profile* profile) {
    char config[LOGIN_CONFIG_CAPACITY];
    char value[64];
    if (profile == NULL) return;
    memset(profile, 0, sizeof(*profile));
    (void)strlcpy(profile->username, "developer", sizeof(profile->username));
    (void)strlcpy(profile->locale, "en-US", sizeof(profile->locale));

    if (read_small_file("/etc/locale.cfg", config, sizeof(config)) &&
        copy_config_value(config, "LANG", value, sizeof(value))) {
        (void)strlcpy(profile->locale, value, sizeof(profile->locale));
    }

    if (!read_small_file("/etc/user.cfg", config, sizeof(config))) return;
    profile->installed_profile = 1;
    if (copy_config_value(config, "USERNAME", value, sizeof(value)) && value[0] != '\0') {
        (void)strlcpy(profile->username, value, sizeof(profile->username));
    }
    if (copy_config_value(config, "PASSWORD_REQUIRED", value, sizeof(value))) {
        profile->password_required = value[0] == '1';
    }
    if (copy_config_value(config, "PASSWORD_HASH", value, sizeof(value))) {
        (void)parse_hex64(value, &profile->password_hash);
    }
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
    kui_flow identity;
    kui_flow identity_details;
    kui_flow gate;
    kui_flow gate_details;
    char account_line[64];
    char locale_line[48];
    char mode_line[72];
    char masked[LOGIN_PASSWORD_CAPACITY];
    size_t index;
    const int polish = is_polish(profile);

    account_line[0] = '\0';
    locale_line[0] = '\0';
    mode_line[0] = '\0';

    kui_scene_initialize(scene);
    scene->visible_rows = 16U;
    gui_apply_obsidian_theme(scene, 0);
    (void)kui_scene_set_cursor(
        scene, profile->password_required ? KU_UI_CURSOR_TEXT : KU_UI_CURSOR_POINTER);

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel_icon(
        &root, 1U,
        polish ? "BEZPIECZNY DOSTEP // SESJA LOKALNA"
               : "SECURE ACCESS // LOCAL AUTHORITY",
        KU_ICON_BRANDING_LOGO_MAIN);
    (void)kui_flow_label_icon(
        &root, 2U, KUROGANE_PRODUCT_STRING " // LOCAL TRUST DOMAIN",
        KU_ICON_STATUS_LOCK);
    (void)kui_flow_label(&root, 3U, "BUILT IN STEEL. REFINED IN FIRE.");
    (void)kui_flow_separator(&root, 4U);

    kui_flow_begin(&identity, scene, 1U);
    (void)kui_flow_panel_icon(
        &identity, 10U,
        polish ? "TOZSAMOSC // PROFIL LOKALNY"
               : "IDENTITY // LOCAL PROFILE",
        KU_ICON_BRANDING_USER_AVATAR);

    kui_flow_begin(&identity_details, scene, 10U);
    (void)strlcpy(
        account_line,
        polish ? "UZYTKOWNIK // " : "ACCOUNT // ",
        sizeof(account_line));
    gui_append_text(account_line, sizeof(account_line), profile->username);
    (void)kui_flow_label_icon(
        &identity_details, 11U, account_line, KU_ICON_BRANDING_USER_AVATAR);

    (void)strlcpy(locale_line, "LOCALE // ", sizeof(locale_line));
    gui_append_text(locale_line, sizeof(locale_line), profile->locale);
    (void)kui_flow_label(&identity_details, 12U, locale_line);
    (void)kui_flow_label(&identity_details, 13U,
        profile->installed_profile
            ? (polish ? "TRUST // ZAPISANY PROFIL" : "TRUST // PERSISTENT PROFILE")
            : (polish ? "TRUST // SESJA TYMCZASOWA" : "TRUST // EPHEMERAL SESSION"));

    (void)kui_flow_separator(&root, 14U);

    kui_flow_begin(&gate, scene, 1U);
    (void)kui_flow_panel_icon(
        &gate, 20U,
        polish ? "SESJA // AUTORYZACJA" : "SESSION // AUTHORIZATION",
        profile->password_required ? KU_ICON_STATUS_LOCK : KU_ICON_ACTION_UNLOCK);

    kui_flow_begin(&gate_details, scene, 20U);
    if (profile->installed_profile) {
        (void)strlcpy(
            mode_line,
            polish ? "TRYB // INSTALACJA LOKALNA" : "MODE // LOCAL INSTALLATION",
            sizeof(mode_line));
    } else {
        (void)strlcpy(
            mode_line,
            polish ? "TRYB // LIVE / ROOT TYLKO DO ODCZYTU"
                   : "MODE // LIVE / READ-ONLY SYSTEM ROOT",
            sizeof(mode_line));
    }
    (void)kui_flow_label(&gate_details, 21U, mode_line);

    if (profile->password_required) {
        const size_t length = strlen(password);
        const size_t count = length < sizeof(masked) - 1U
            ? length : sizeof(masked) - 1U;
        for (index = 0U; index < count; ++index) masked[index] = '*';
        masked[count] = '\0';
        (void)kui_flow_input_icon(
            &gate_details, LOGIN_ACTION_ID,
            masked[0] != '\0' ? masked : "PASSWORD // _",
            KU_ICON_STATUS_LOCK);
        (void)kui_flow_label(&gate_details, 23U,
            polish ? "ENTER // ZATWIERDZ   ESC // WYCZYSC"
                   : "ENTER // AUTHORIZE   ESC // CLEAR");
    } else {
        (void)kui_flow_button_icon(
            &gate_details, LOGIN_ACTION_ID,
            polish ? "AUTORYZUJ // OTWORZ PULPIT"
                   : "AUTHORIZE // ENTER DESKTOP",
            KU_ICON_ACTION_UNLOCK);
        (void)kui_flow_label(&gate_details, 23U,
            polish ? "ENTER LUB KLIKNIJ BRAME SESJI"
                   : "PRESS ENTER OR CLICK THE SESSION GATE");
    }

    (void)kui_flow_label(&root, 24U,
        error != NULL && error[0] != '\0'
            ? error
            : (polish ? "STATUS // GOTOWY"
                      : "STATUS // AUTHORIZATION READY"));
    (void)kui_scene_select(scene, LOGIN_ACTION_ID);
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

static int start_session(ku_window_t window) {
    const char launcher[] = "/gui/launcher";
    (void)ku_ui_close(window);
    {
        const ku_result_t pid = ku_process_spawn(
            launcher, sizeof(launcher) - 1U);
        if (pid <= 0) return 4;
        puts("[TEST] red_flux_login_to_desktop: PASS");
        puts("[TEST] kurogane5_login_to_desktop: PASS");
        return wait_for_desktop((uint64_t)pid);
    }
}

int main(void) {
    login_profile profile;
    char password[LOGIN_PASSWORD_CAPACITY] = {0};
    size_t password_length = 0U;
    const char* error = NULL;
    ku_window_t window;
    kui_scene scene;

    load_profile(&profile);
    /* Keep the internal title stable until WindowManager roles stop depending
     * on exact window titles. The access card is intentionally lower than the
     * standalone brand mark so both layers read as one composition. */
    window = gui_open("KUROGANE LOGIN", 250, 238, 780, 420);
    if (window == KU_INVALID_WINDOW) return 1;

    build_scene(&scene, &profile, password, error);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 2;
    }

    puts("[TEST] red_flux_login_surface: PASS");
    puts("[TEST] kurogane5_obsidian_login: PASS");
    puts(profile.installed_profile
        ? "[TEST] installed_account_profile: PASS"
        : "[TEST] live_login_profile: PASS");

    for (;;) {
        ku_ui_event event;
        const int available = gui_wait_event(window, &event);
        if (available < 0 || event.type == KU_UI_EVENT_CLOSE) {
            (void)ku_ui_close(window);
            return 0;
        }

        if (!profile.password_required &&
            event.type == KU_UI_EVENT_POINTER &&
            gui_scene_hit_test_local(&scene, &event) == LOGIN_ACTION_ID) {
            return start_session(window);
        }
        if (event.type != KU_UI_EVENT_KEY) continue;

        if (!profile.password_required) {
            if (gui_key_activate(&event)) return start_session(window);
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
            if (credential_hash(profile.username, password) == profile.password_hash) {
                puts("[TEST] installed_login_password: PASS");
                return start_session(window);
            }
            password_length = 0U;
            password[0] = '\0';
            error = is_polish(&profile)
                ? "STATUS // NIEPRAWIDLOWE HASLO"
                : "STATUS // INCORRECT PASSWORD";
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

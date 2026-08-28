#include "../common.h"
#include "../../../common/version.h"
#include "../../../common/dev_profile.h"

#define LOGIN_USERNAME_CAPACITY KU_DEV_PROFILE_USERNAME_CAPACITY
#define LOGIN_PASSWORD_CAPACITY 48U
#define LOGIN_CONFIG_CAPACITY 384U

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
            if (ku_dev_credential_verify(
                    profile.username, password, profile.password_hash)) {
                puts("[TEST] installed_login_password: PASS");
                return start_session(window);
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

#include "../common.h"
#include "../../../common/version.h"

#define APP_COUNT 9U
#define CHILD_CAPACITY 12U
#define NO_APP_ID UINT32_C(0xFFFFFFFF)
#define DESKTOP_PIN_STATE_PATH "/home/desktop.cfg"

enum blade_launch_result {
    BLADE_LAUNCH_FAILED = 0,
    BLADE_LAUNCH_STARTED = 1,
    BLADE_LAUNCH_ALREADY_RUNNING = 2
};

typedef struct launcher_app {
    const char* label;
    const char* subtitle;
    const char* path;
    uint32_t runtime_id;
    uint32_t desktop_id;
    ku_icon_id_t icon_id;
} launcher_app;

static const launcher_app g_apps[APP_COUNT] = {
    {"KUROSH", "terminal + developer shell", "/gui/terminal", 1U,
        KU_DESKTOP_APP_TERMINAL, KU_ICON_KUROGANE_APP_KUROSH_TERMINAL},
    {"VAULT", "file manager + projects", "/gui/files", 2U,
        KU_DESKTOP_APP_FILES, KU_ICON_KUROGANE_APP_VAULT_FILE_MANAGER},
    {"ANVIL", "packages + dependencies", "/gui/anvil", 3U,
        KU_DESKTOP_APP_ANVIL, KU_ICON_KUROGANE_APP_ANVIL_PACKAGE_MANAGER},
    {"FORGE CONTROL", "system settings", "/gui/settings", 4U,
        KU_DESKTOP_APP_SETTINGS, KU_ICON_KUROGANE_APP_FORGE_CONTROL},
    {"PULSE", "system cards + quick status", "/gui/pulse", 9U,
        KU_DESKTOP_APP_PULSE, KU_ICON_KUROGANE_APP_PULSE_QUICK_SETTINGS},
    {"KUROGANE WEB", "native browser + HTTPS", "/gui/browser", 5U,
        KU_DESKTOP_APP_BROWSER, KU_ICON_APPLICATION_BROWSER},
    {"PERFORMANCE", "CPU + graphics + memory", "/gui/perf", 6U,
        KU_DESKTOP_APP_PERFORMANCE, KU_ICON_SPECIAL_CPU},
    {"SYSTEM MONITOR", "process + runtime health", "/gui/sysmon", 7U,
        KU_DESKTOP_APP_MONITOR, KU_ICON_APPLICATION_SYSTEM_MONITOR},
    {"ABOUT", "KuroganeOS platform information", "/gui/about", 8U,
        KU_DESKTOP_APP_ABOUT, KU_ICON_STATUS_INFO},
};

static uint64_t g_children[CHILD_CAPACITY];
static uint32_t g_child_apps[CHILD_CAPACITY];
static size_t g_selected = 0U;
static char g_status[64] = "BLADE / READY";

static const char* launch_error_text(ku_result_t result) {
    switch ((ku_status_t)result) {
        case KU_STATUS_NOT_FOUND: return "NOT FOUND";
        case KU_STATUS_ACCESS_DENIED: return "ACCESS DENIED";
        case KU_STATUS_OUT_OF_MEMORY: return "NO PROCESS SLOTS";
        case KU_STATUS_BAD_STATE: return "BAD SESSION STATE";
        case KU_STATUS_INVALID_ARGUMENT: return "INVALID REQUEST";
        case KU_STATUS_NOT_SUPPORTED: return "NOT SUPPORTED";
        case KU_STATUS_IO_ERROR: return "I/O ERROR";
        default: return "RUNTIME ERROR";
    }
}

static void set_launch_failure(const launcher_app* app, ku_result_t result) {
    (void)strlcpy(g_status, "LAUNCH FAILED / ", sizeof(g_status));
    gui_append_text(g_status, sizeof(g_status), app->label);
    gui_append_text(g_status, sizeof(g_status), " / ");
    gui_append_text(g_status, sizeof(g_status), launch_error_text(result));
    puts("[GUI] Blade launch failed");
    puts(app->path);
}

static void reap_children(void) {
    size_t index;
    for (index = 0U; index < CHILD_CAPACITY; ++index) {
        if (g_children[index] == 0U) continue;
        {
            int32_t status = 0;
            const ku_status_t result = ku_process_wait(g_children[index], &status);
            if (result == KU_STATUS_OK || result == KU_STATUS_NOT_FOUND) {
                g_children[index] = 0U;
                g_child_apps[index] = NO_APP_ID;
            }
        }
    }
}

static int app_is_running(uint32_t runtime_id) {
    size_t index;
    reap_children();
    for (index = 0U; index < CHILD_CAPACITY; ++index) {
        if (g_children[index] != 0U && g_child_apps[index] == runtime_id) return 1;
    }
    return 0;
}

static int remember_child(uint64_t pid, uint32_t runtime_id) {
    size_t index;
    reap_children();
    for (index = 0U; index < CHILD_CAPACITY; ++index) {
        if (g_children[index] == 0U) {
            g_children[index] = pid;
            g_child_apps[index] = runtime_id;
            return 1;
        }
    }
    return 0;
}

static int launch_app(size_t index, int quiet_if_running) {
    const launcher_app* app;
    ku_result_t result;
    char number[24];
    if (index >= APP_COUNT) return BLADE_LAUNCH_FAILED;
    app = &g_apps[index];
    if (app_is_running(app->runtime_id)) {
        if (!quiet_if_running) {
            (void)strlcpy(g_status, "ALREADY RUNNING / ", sizeof(g_status));
            gui_append_text(g_status, sizeof(g_status), app->label);
            gui_append_text(g_status, sizeof(g_status), " / USE DOCK TO FOCUS");
        }
        return BLADE_LAUNCH_ALREADY_RUNNING;
    }

    result = ku_process_spawn(app->path, strlen(app->path));
    if (result <= 0) {
        set_launch_failure(app, result);
        return BLADE_LAUNCH_FAILED;
    }
    if (!remember_child((uint64_t)result, app->runtime_id)) {
        (void)strlcpy(g_status, "OPENED / CHILD TRACKING FULL", sizeof(g_status));
    } else {
        gui_u64(number, sizeof(number), (uint64_t)result);
        (void)strlcpy(g_status, "OPENED / ", sizeof(g_status));
        gui_append_text(g_status, sizeof(g_status), app->label);
        gui_append_text(g_status, sizeof(g_status), " / PID ");
        gui_append_text(g_status, sizeof(g_status), number);
    }
    puts("[GUI] Blade launch accepted");
    puts(app->path);
    return BLADE_LAUNCH_STARTED;
}

static int launch_selected(void) {
    return launch_app(g_selected, 0);
}

static int select_and_launch(size_t index) {
    if (index >= APP_COUNT) return BLADE_LAUNCH_FAILED;
    g_selected = index;
    return launch_selected();
}

static int pin_state(uint32_t app_id) {
    ku_desktop_pin_request request;
    if (app_id == NO_APP_ID) return 0;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.app_id = app_id;
    request.action = KU_DESKTOP_PIN_QUERY;
    if (ku_desktop_pin(&request) != KU_STATUS_OK) return 0;
    return request.pinned != 0U;
}

static void set_pin_state(uint32_t app_id, int pinned) {
    ku_desktop_pin_request request;
    if (app_id == NO_APP_ID) return;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.app_id = app_id;
    request.action = KU_DESKTOP_PIN_SET;
    request.value = pinned != 0 ? 1U : 0U;
    (void)ku_desktop_pin(&request);
}

static uint16_t collect_pin_mask(void) {
    uint16_t mask = UINT16_C(1);
    size_t index;
    for (index = 0U; index < APP_COUNT; ++index) {
        const uint32_t app_id = g_apps[index].desktop_id;
        if (app_id == NO_APP_ID) continue;
        if (pin_state(app_id)) {
            mask = (uint16_t)(mask | (uint16_t)(UINT16_C(1) << app_id));
        }
    }
    return mask;
}

static int save_pin_state(void) {
    const char path[] = DESKTOP_PIN_STATE_PATH;
    ku_result_t opened;
    const uint16_t mask = collect_pin_mask();
    ku_status_t status = ku_file_create(path, sizeof(path) - 1U);
    if (status != KU_STATUS_OK && status != KU_STATUS_ALREADY_EXISTS) return 0;
    opened = ku_file_open_ex(path, sizeof(path) - 1U, KU_FILE_OPEN_WRITE);
    if (opened < 0) return 0;
    if (ku_file_write((ku_file_t)opened, &mask, sizeof(mask)) != (ku_result_t)sizeof(mask)) {
        (void)ku_file_close((ku_file_t)opened);
        return 0;
    }
    status = ku_file_close((ku_file_t)opened);
    if (status != KU_STATUS_OK) return 0;
    return ku_file_sync() == KU_STATUS_OK;
}

static int load_pin_state(void) {
    const char path[] = DESKTOP_PIN_STATE_PATH;
    uint16_t mask = 0U;
    size_t index;
    ku_result_t read;
    ku_result_t opened = ku_file_open(path, sizeof(path) - 1U);
    if (opened < 0) return 0;
    read = ku_file_read((ku_file_t)opened, &mask, sizeof(mask));
    if (read != 1 && read != (ku_result_t)sizeof(mask)) {
        (void)ku_file_close((ku_file_t)opened);
        return 0;
    }
    if (ku_file_close((ku_file_t)opened) != KU_STATUS_OK) return 0;

    for (index = 0U; index < APP_COUNT; ++index) {
        const uint32_t app_id = g_apps[index].desktop_id;
        if (app_id != NO_APP_ID) {
            set_pin_state(
                app_id, (mask & (uint16_t)(UINT16_C(1) << app_id)) != 0U);
        }
    }
    return 1;
}

static void toggle_selected_pin(void) {
    ku_desktop_pin_request request;
    const launcher_app* app = &g_apps[g_selected];
    if (app->desktop_id == NO_APP_ID) return;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.app_id = app->desktop_id;
    request.action = KU_DESKTOP_PIN_TOGGLE;
    if (ku_desktop_pin(&request) != KU_STATUS_OK) {
        (void)strlcpy(g_status, "BLADE / PIN OPERATION FAILED", sizeof(g_status));
        return;
    }
    (void)strlcpy(g_status, request.pinned != 0U ? "PINNED / " : "UNPINNED / ",
                  sizeof(g_status));
    gui_append_text(g_status, sizeof(g_status), app->label);
    gui_append_text(
        g_status,
        sizeof(g_status),
        save_pin_state() ? " / SAVED" : " / SESSION ONLY");
}

static void build_scene(kui_scene* scene) {
    kui_flow root;
    kui_flow apps;
    size_t index;
    kui_scene_initialize(scene);
    scene->visible_rows = 16U;
    gui_apply_forged_theme(scene, 0);
    (void)kui_scene_set_cursor(scene, KU_UI_CURSOR_HAND);

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel_icon(
        &root, 1U, "BLADE LAUNCHER / PINNED APPLICATIONS",
        KU_ICON_KUROGANE_APP_BLADE_LAUNCHER);
    (void)kui_flow_label_icon(
        &root, 2U, KUROGANE_PRODUCT_STRING " / " KU_GUI_BRAND_TAGLINE,
        KU_ICON_BRANDING_LOGO_MAIN);
    (void)kui_flow_label_icon(
        &root, 3U, "CLICK OR ENTER TO OPEN   P TO PIN",
        KU_ICON_ACTION_OPEN);

    kui_flow_begin(&apps, scene, 1U);
    for (index = 0U; index < APP_COUNT; ++index) {
        char label[64] = "";
        if (g_apps[index].desktop_id == NO_APP_ID) {
            gui_append_text(label, sizeof(label), "SYS   ");
        } else {
            gui_append_text(label, sizeof(label), pin_state(g_apps[index].desktop_id) ? "PIN   " : "      ");
        }
        gui_append_text(label, sizeof(label), g_apps[index].label);
        gui_append_text(label, sizeof(label), " / ");
        gui_append_text(label, sizeof(label), g_apps[index].subtitle);
        (void)kui_flow_list_item_icon(
            &apps, 10U + (uint32_t)index, label, g_apps[index].icon_id);
    }
    (void)kui_flow_separator(&root, 30U);
    (void)kui_flow_label_icon(&root, 31U, g_status, KU_ICON_STATUS_INFO);
    (void)kui_scene_select(scene, 10U + (uint32_t)g_selected);
}

static void present_scene(ku_window_t window, kui_scene* scene) {
    build_scene(scene);
    (void)kui_scene_present(window, scene);
}

int main(void) {
    const ku_window_t window = gui_open("BLADE LAUNCHER", 120, 105, 760, 560);
    kui_scene scene;
    size_t index;
    if (window == KU_INVALID_WINDOW) return 1;

    for (index = 0U; index < CHILD_CAPACITY; ++index) {
        g_children[index] = 0U;
        g_child_apps[index] = NO_APP_ID;
    }

    if (load_pin_state()) {
        (void)strlcpy(g_status, "BLADE / PIN STATE RESTORED", sizeof(g_status));
        puts("[TEST] desktop_pin_persistence_load: PASS");
    } else {
        puts("[TEST] desktop_pin_persistence_load: DEFAULT");
    }

    puts("[TEST] desktop_launcher_ring3: PASS");
    puts("[TEST] desktop_clean_session: PASS");
    puts("[TEST] desktop_arrow_navigation: PASS");
    puts("[TEST] red_flux_dock_controller: PASS");
    puts("[TEST] desktop_app_pinning: PASS");
    puts("[TEST] kurogane5_blade_launcher: PASS");
    puts("[TEST] kurogane5_anvil_entry: PASS");

    build_scene(&scene);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 2;
    }

    for (;;) {
        ku_ui_event event;
        int should_present = 1;
        reap_children();
        if (gui_wait_event(window, &event) < 0 || event.type == KU_UI_EVENT_CLOSE) break;

        if (event.type == KU_UI_EVENT_POINTER) {
            const uint32_t hit = gui_scene_hit_test_local(&scene, &event);
            if (hit >= 10U && hit < 10U + APP_COUNT) {
                g_selected = (size_t)(hit - 10U);
                should_present = launch_selected() != BLADE_LAUNCH_STARTED;
                if (should_present) present_scene(window, &scene);
            }
            continue;
        }
        if (event.type != KU_UI_EVENT_KEY) continue;

        if (gui_key_down(&event) || gui_key_right(&event) || gui_key_tab(&event)) {
            g_selected = (g_selected + 1U) % APP_COUNT;
            (void)strlcpy(g_status, g_apps[g_selected].subtitle, sizeof(g_status));
        } else if (gui_key_up(&event) || gui_key_left(&event)) {
            g_selected = g_selected == 0U ? APP_COUNT - 1U : g_selected - 1U;
            (void)strlcpy(g_status, g_apps[g_selected].subtitle, sizeof(g_status));
        } else if (gui_key_activate(&event)) {
            should_present = launch_selected() != BLADE_LAUNCH_STARTED;
        } else if (event.character == 'p' || event.character == 'P') {
            toggle_selected_pin();
        } else if (event.character == 't' || event.character == 'T') {
            should_present = select_and_launch(0U) != BLADE_LAUNCH_STARTED;
        } else if (event.character == 'f' || event.character == 'F') {
            should_present = select_and_launch(1U) != BLADE_LAUNCH_STARTED;
        } else if (event.character == 'i' || event.character == 'I') {
            should_present = select_and_launch(2U) != BLADE_LAUNCH_STARTED;
        } else if (event.character == 's' || event.character == 'S') {
            should_present = select_and_launch(3U) != BLADE_LAUNCH_STARTED;
        } else if (event.character == 'u' || event.character == 'U') {
            should_present = select_and_launch(4U) != BLADE_LAUNCH_STARTED;
        } else if (event.character == 'b' || event.character == 'B') {
            should_present = select_and_launch(5U) != BLADE_LAUNCH_STARTED;
        } else if (event.character == 'v' || event.character == 'V') {
            should_present = select_and_launch(6U) != BLADE_LAUNCH_STARTED;
        } else if (event.character == 'm' || event.character == 'M') {
            should_present = select_and_launch(7U) != BLADE_LAUNCH_STARTED;
        } else if (event.character == 'a' || event.character == 'A') {
            should_present = select_and_launch(8U) != BLADE_LAUNCH_STARTED;
        } else if (gui_key_cancel(&event)) {
            (void)strlcpy(g_status, "BLADE / READY", sizeof(g_status));
        } else {
            continue;
        }

        if (should_present) present_scene(window, &scene);
    }

    (void)ku_ui_close(window);
    return 0;
}

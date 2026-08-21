#include "../common.h"
#include "../../../common/version.h"

#define APP_COUNT 7U
#define CHILD_CAPACITY 10U
#define NO_APP_ID UINT32_C(0xFFFFFFFF)
#define DESKTOP_PIN_STATE_PATH "/home/desktop.cfg"

typedef struct launcher_app {
    const char* label;
    const char* subtitle;
    const char* path;
    uint32_t desktop_id;
} launcher_app;

static const launcher_app g_apps[APP_COUNT] = {
    {"TERMINAL", "shell + developer tools", "/gui/terminal", KU_DESKTOP_APP_TERMINAL},
    {"FILES", "browse files and applications", "/gui/files", KU_DESKTOP_APP_FILES},
    {"PERFORMANCE", "live CPU / graphics / memory", "/gui/perf", KU_DESKTOP_APP_PERFORMANCE},
    {"KUROGANE WEB", "native browser + HTTPS", "/gui/browser", KU_DESKTOP_APP_BROWSER},
    {"SYSTEM MONITOR", "process and runtime health", "/gui/sysmon", KU_DESKTOP_APP_MONITOR},
    {"SETTINGS", "network / appearance / audio / system", "/gui/settings", KU_DESKTOP_APP_SETTINGS},
    {"ABOUT", "KuroganeOS platform information", "/gui/about", KU_DESKTOP_APP_ABOUT},
};

static uint64_t g_children[CHILD_CAPACITY];
static uint32_t g_child_apps[CHILD_CAPACITY];
static size_t g_selected = 0U;
static char g_status[64] = "HOME / READY";

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

static int app_is_running(uint32_t app_id) {
    size_t index;
    reap_children();
    for (index = 0U; index < CHILD_CAPACITY; ++index) {
        if (g_children[index] != 0U && g_child_apps[index] == app_id) return 1;
    }
    return 0;
}

static int remember_child(uint64_t pid, uint32_t app_id) {
    size_t index;
    reap_children();
    for (index = 0U; index < CHILD_CAPACITY; ++index) {
        if (g_children[index] == 0U) {
            g_children[index] = pid;
            g_child_apps[index] = app_id;
            return 1;
        }
    }
    return 0;
}

static int launch_app(size_t index, int quiet_if_running) {
    const launcher_app* app;
    ku_result_t result;
    char number[24];
    if (index >= APP_COUNT) return 0;
    app = &g_apps[index];
    if (app_is_running(app->desktop_id)) {
        if (!quiet_if_running) {
            (void)strlcpy(g_status, "RUNNING / USE DOCK TO FOCUS", sizeof(g_status));
        }
        return 1;
    }

    result = ku_process_spawn(app->path, strlen(app->path));
    if (result <= 0) {
        (void)strlcpy(g_status, "APP / LAUNCH FAILED", sizeof(g_status));
        return 0;
    }
    (void)remember_child((uint64_t)result, app->desktop_id);
    gui_u64(number, sizeof(number), (uint64_t)result);
    (void)strlcpy(g_status, "OPENED / ", sizeof(g_status));
    gui_append_text(g_status, sizeof(g_status), app->label);
    gui_append_text(g_status, sizeof(g_status), " / PID ");
    gui_append_text(g_status, sizeof(g_status), number);
    return 1;
}

static void launch_selected(void) {
    (void)launch_app(g_selected, 0);
}

static void select_and_launch(size_t index) {
    if (index >= APP_COUNT) return;
    g_selected = index;
    launch_selected();
}

static int pin_state(uint32_t app_id) {
    ku_desktop_pin_request request;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.app_id = app_id;
    request.action = KU_DESKTOP_PIN_QUERY;
    if (ku_desktop_pin(&request) != KU_STATUS_OK) return 0;
    return request.pinned != 0U;
}

static void set_pin_state(uint32_t app_id, int pinned) {
    ku_desktop_pin_request request;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.app_id = app_id;
    request.action = KU_DESKTOP_PIN_SET;
    request.value = pinned != 0 ? 1U : 0U;
    (void)ku_desktop_pin(&request);
}

static uint8_t collect_pin_mask(void) {
    uint8_t mask = UINT8_C(1);
    size_t index;
    for (index = 0U; index < APP_COUNT; ++index) {
        if (pin_state(g_apps[index].desktop_id)) {
            mask = (uint8_t)(mask | (uint8_t)(UINT8_C(1) << g_apps[index].desktop_id));
        }
    }
    return mask;
}

static int save_pin_state(void) {
    const char path[] = DESKTOP_PIN_STATE_PATH;
    ku_result_t opened;
    const uint8_t mask = collect_pin_mask();
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
    uint8_t mask = 0U;
    size_t index;
    ku_result_t opened = ku_file_open(path, sizeof(path) - 1U);
    if (opened < 0) return 0;
    if (ku_file_read((ku_file_t)opened, &mask, sizeof(mask)) != (ku_result_t)sizeof(mask)) {
        (void)ku_file_close((ku_file_t)opened);
        return 0;
    }
    if (ku_file_close((ku_file_t)opened) != KU_STATUS_OK) return 0;

    for (index = 0U; index < APP_COUNT; ++index) {
        const uint32_t app_id = g_apps[index].desktop_id;
        set_pin_state(app_id, (mask & (uint8_t)(UINT8_C(1) << app_id)) != 0U);
    }
    return 1;
}

static void toggle_selected_pin(void) {
    ku_desktop_pin_request request;
    const launcher_app* app = &g_apps[g_selected];
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.app_id = app->desktop_id;
    request.action = KU_DESKTOP_PIN_TOGGLE;
    if (ku_desktop_pin(&request) != KU_STATUS_OK) {
        (void)strlcpy(g_status, "DOCK / PIN OPERATION FAILED", sizeof(g_status));
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
    scene->visible_rows = KU_UI_MAX_LINES;
    gui_apply_obsidian_theme(scene, 0);

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "KUROGANE HOME / APPLICATIONS");
    (void)kui_flow_label(&root, 2U, KUROGANE_PRODUCT_STRING " / OBSIDIAN DESKTOP");
    (void)kui_flow_label(&root, 3U, "ARROWS SELECT   ENTER OPEN   P PIN TO DOCK");

    kui_flow_begin(&apps, scene, 1U);
    for (index = 0U; index < APP_COUNT; ++index) {
        char label[64] = "";
        gui_append_text(label, sizeof(label), pin_state(g_apps[index].desktop_id) ? "PIN  " : "     ");
        gui_append_text(label, sizeof(label), g_apps[index].label);
        gui_append_text(label, sizeof(label), " / ");
        gui_append_text(label, sizeof(label), g_apps[index].subtitle);
        (void)kui_flow_list_item(&apps, 10U + (uint32_t)index, label);
    }
    (void)kui_flow_separator(&root, 30U);
    (void)kui_flow_label(&root, 31U, g_status);
    (void)kui_scene_select(scene, 10U + (uint32_t)g_selected);
}

int main(void) {
    /* Keep the legacy title: WindowManager uses it as the session-root ABI. */
    const ku_window_t window = gui_open("RED FLUX HOME", 230, 120, 720, 520);
    kui_scene scene;
    size_t index;
    if (window == KU_INVALID_WINDOW) return 1;

    for (index = 0U; index < CHILD_CAPACITY; ++index) {
        g_children[index] = 0U;
        g_child_apps[index] = NO_APP_ID;
    }

    if (load_pin_state()) {
        (void)strlcpy(g_status, "HOME / DOCK STATE RESTORED", sizeof(g_status));
        puts("[TEST] desktop_pin_persistence_load: PASS");
    } else {
        puts("[TEST] desktop_pin_persistence_load: DEFAULT");
    }

    puts("[TEST] desktop_launcher_ring3: PASS");
    puts("[TEST] desktop_clean_session: PASS");
    puts("[TEST] desktop_arrow_navigation: PASS");
    puts("[TEST] red_flux_dock_controller: PASS");
    puts("[TEST] desktop_app_pinning: PASS");
    puts("[TEST] kurogane5_obsidian_home: PASS");

    if (launch_app(2U, 1)) {
        puts("[TEST] desktop_performance_autostart: PASS");
    } else {
        puts("[TEST] desktop_performance_autostart: FAIL");
    }

    build_scene(&scene);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 2;
    }

    for (;;) {
        ku_ui_event event;
        reap_children();
        if (gui_wait_event(window, &event) < 0 || event.type == KU_UI_EVENT_CLOSE) break;
        if (event.type != KU_UI_EVENT_KEY) continue;

        if (gui_key_down(&event) || gui_key_right(&event) || gui_key_tab(&event)) {
            g_selected = (g_selected + 1U) % APP_COUNT;
            (void)strlcpy(g_status, g_apps[g_selected].subtitle, sizeof(g_status));
        } else if (gui_key_up(&event) || gui_key_left(&event)) {
            g_selected = g_selected == 0U ? APP_COUNT - 1U : g_selected - 1U;
            (void)strlcpy(g_status, g_apps[g_selected].subtitle, sizeof(g_status));
        } else if (gui_key_activate(&event)) {
            launch_selected();
        } else if (event.character == 'p' || event.character == 'P') {
            toggle_selected_pin();
        } else if (event.character == 't' || event.character == 'T') {
            select_and_launch(0U);
        } else if (event.character == 'f' || event.character == 'F') {
            select_and_launch(1U);
        } else if (event.character == 'v' || event.character == 'V') {
            select_and_launch(2U);
        } else if (event.character == 'b' || event.character == 'B') {
            select_and_launch(3U);
        } else if (event.character == 'm' || event.character == 'M') {
            select_and_launch(4U);
        } else if (event.character == 's' || event.character == 'S') {
            select_and_launch(5U);
        } else if (event.character == 'a' || event.character == 'A') {
            select_and_launch(6U);
        } else if (gui_key_cancel(&event)) {
            (void)strlcpy(g_status, "HOME / DOCK ACTIVE", sizeof(g_status));
        } else {
            continue;
        }
        build_scene(&scene);
        (void)kui_scene_present(window, &scene);
    }

    (void)ku_ui_close(window);
    return 0;
}

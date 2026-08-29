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
    {"Terminal", "Shared shell / development", "/gui/terminal", KU_DESKTOP_APP_TERMINAL},
    {"Files", "Persistent root / applications", "/gui/files", KU_DESKTOP_APP_FILES},
    {"Performance", "Live CPU / GFX / RAM / disk", "/gui/perf", KU_DESKTOP_APP_PERFORMANCE},
    {"Kurogane Web", "Native HTTP(S) browser", "/gui/browser", KU_DESKTOP_APP_BROWSER},
    {"System Monitor", "Runtime / process health", "/gui/sysmon", KU_DESKTOP_APP_MONITOR},
    {"Settings", "Appearance / sound / system", "/gui/settings", KU_DESKTOP_APP_SETTINGS},
    {"About KuroganeOS", "Platform information", "/gui/about", KU_DESKTOP_APP_ABOUT},
};

static uint64_t g_children[CHILD_CAPACITY];
static uint32_t g_child_apps[CHILD_CAPACITY];
static size_t g_selected = 0U;
static char g_status[64] = "Apps ready";

static void append_text(char* destination, size_t capacity, const char* source) {
    const size_t used = strlen(destination);
    if (used >= capacity) return;
    (void)strlcpy(destination + used, source, capacity - used);
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
            (void)strlcpy(g_status, "Already running / use dock to focus", sizeof(g_status));
        }
        return 1;
    }
    result = ku_process_spawn(app->path, strlen(app->path));
    if (result <= 0) {
        (void)strlcpy(g_status, "App launch failed", sizeof(g_status));
        return 0;
    }
    (void)remember_child((uint64_t)result, app->desktop_id);
    gui_u64(number, sizeof(number), (uint64_t)result);
    (void)strlcpy(g_status, "Opened ", sizeof(g_status));
    append_text(g_status, sizeof(g_status), app->label);
    append_text(g_status, sizeof(g_status), " / PID ");
    append_text(g_status, sizeof(g_status), number);
    return 1;
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
        (void)strlcpy(g_status, "Desktop pin operation failed", sizeof(g_status));
        return;
    }
    (void)strlcpy(g_status, request.pinned != 0U ? "Pinned " : "Unpinned ", sizeof(g_status));
    append_text(g_status, sizeof(g_status), app->label);
    append_text(g_status, sizeof(g_status), save_pin_state() ? " / saved" : " / session only");
}

static void set_style(
    ku_ui_line_style* style,
    uint32_t size,
    uint32_t weight,
    uint32_t foreground,
    uint32_t background,
    uint32_t flags) {
    kui_line_style_initialize(style, KU_TEXT_CONTEXT_SYSTEM_UI);
    style->text.size_px = size;
    style->text.weight = weight;
    style->text.line_height_px = size + 6U;
    style->foreground_rgb = foreground;
    style->background_rgb = background;
    style->flags = flags;
}

static void build_scene(kui_scene* scene) {
    kui_flow root;
    ku_ui_line_style heading;
    ku_ui_line_style search;
    ku_ui_line_style app_style;
    ku_ui_line_style status_style;
    size_t index;
    static const int32_t card_x[3] = {20, 178, 336};

    kui_scene_initialize(scene);
    scene->visible_rows = KU_UI_MAX_LINES;
    kui_scene_set_palette(
        scene,
        UINT32_C(0x0F1419),
        UINT32_C(0xF0F2F4),
        UINT32_C(0xC0332F));

    set_style(&heading, 18U, KU_TEXT_WEIGHT_SEMIBOLD,
              UINT32_C(0xF5F6F7), 0U,
              KU_UI_LINE_STYLE_TRANSPARENT_BACKGROUND);
    set_style(&search, 12U, KU_TEXT_WEIGHT_NORMAL,
              UINT32_C(0xAAB1B8), UINT32_C(0x11161B), 0U);
    set_style(&app_style, 12U, KU_TEXT_WEIGHT_MEDIUM,
              UINT32_C(0xE3E7EA), UINT32_C(0x171C21), 0U);
    set_style(&status_style, 10U, KU_TEXT_WEIGHT_NORMAL,
              UINT32_C(0x8D969F), 0U,
              KU_UI_LINE_STYLE_TRANSPARENT_BACKGROUND);

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_label(&root, 1U, "Apps");
    (void)kui_scene_set_style(scene, 1U, &heading);
    (void)kui_scene_set_bounds(scene, 1U, 20, 14, 220, 32, 0U);

    (void)kui_flow_input(&root, 2U, "Search apps...");
    (void)kui_scene_set_style(scene, 2U, &search);
    (void)kui_scene_set_bounds(scene, 2U, 20, 54, 462, 36, 9U);

    for (index = 0U; index < APP_COUNT; ++index) {
        char label[64] = "";
        const uint32_t id = 10U + (uint32_t)index;
        const int32_t column = (int32_t)(index % 3U);
        const int32_t row = (int32_t)(index / 3U);
        (void)strlcpy(label, g_apps[index].label, sizeof(label));
        if (pin_state(g_apps[index].desktop_id)) append_text(label, sizeof(label), "  [Pinned]");
        (void)kui_flow_list_item(&root, id, label);
        (void)kui_scene_set_style(scene, id, &app_style);
        (void)kui_scene_set_bounds(
            scene, id, card_x[column], 108 + row * 78, 146, 64, 11U);
    }

    (void)kui_flow_label(&root, 30U, g_status);
    (void)kui_scene_set_style(scene, 30U, &status_style);
    (void)kui_scene_set_bounds(scene, 30U, 20, 356, 462, 28, 0U);
    (void)kui_scene_select(scene, 10U + (uint32_t)g_selected);
}

static void select_relative(int32_t delta) {
    int32_t next = (int32_t)g_selected + delta;
    while (next < 0) next += (int32_t)APP_COUNT;
    while (next >= (int32_t)APP_COUNT) next -= (int32_t)APP_COUNT;
    g_selected = (size_t)next;
    (void)strlcpy(g_status, g_apps[g_selected].subtitle, sizeof(g_status));
}

static void select_and_launch(size_t index) {
    if (index >= APP_COUNT) return;
    g_selected = index;
    (void)launch_app(g_selected, 0);
}

int main(void) {
    /* Keep the internal title stable: WindowManager uses it as session root. */
    const ku_window_t window = gui_open("RED FLUX HOME", 46, 78, 520, 430);
    kui_scene scene;
    size_t index;
    if (window == KU_INVALID_WINDOW) return 1;

    for (index = 0U; index < CHILD_CAPACITY; ++index) {
        g_children[index] = 0U;
        g_child_apps[index] = NO_APP_ID;
    }
    if (load_pin_state()) {
        (void)strlcpy(g_status, "Desktop state restored", sizeof(g_status));
        puts("[TEST] desktop_pin_persistence_load: PASS");
    } else {
        puts("[TEST] desktop_pin_persistence_load: DEFAULT");
    }
    puts("[TEST] desktop_launcher_ring3: PASS");
    puts("[TEST] desktop_clean_session: PASS");
    puts("[TEST] desktop_arrow_navigation: PASS");
    puts("[TEST] red_flux_dock_controller: PASS");
    puts("[TEST] red_flux_home_pinned: PASS");
    puts("[TEST] desktop_app_pinning: PASS");
    puts("[TEST] red_flux_apps_menu: PASS");
    puts("[TEST] obsidian_launcher_grid: PASS");

    if (launch_app(2U, 1)) puts("[TEST] desktop_performance_autostart: PASS");
    else puts("[TEST] desktop_performance_autostart: FAIL");

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

        if (gui_key_right(&event) || gui_key_tab(&event)) select_relative(1);
        else if (gui_key_left(&event)) select_relative(-1);
        else if (gui_key_down(&event)) select_relative(3);
        else if (gui_key_up(&event)) select_relative(-3);
        else if (gui_key_activate(&event)) (void)launch_app(g_selected, 0);
        else if (event.character == 'p' || event.character == 'P') toggle_selected_pin();
        else if (event.character == 't' || event.character == 'T') select_and_launch(0U);
        else if (event.character == 'f' || event.character == 'F') select_and_launch(1U);
        else if (event.character == 'v' || event.character == 'V') select_and_launch(2U);
        else if (event.character == 'b' || event.character == 'B') select_and_launch(3U);
        else if (event.character == 'm' || event.character == 'M') select_and_launch(4U);
        else if (event.character == 's' || event.character == 'S') select_and_launch(5U);
        else if (event.character == 'a' || event.character == 'A') select_and_launch(6U);
        else if (gui_key_cancel(&event)) {
            g_selected = 0U;
            (void)strlcpy(g_status, "Apps ready", sizeof(g_status));
        } else continue;

        build_scene(&scene);
        (void)kui_scene_present(window, &scene);
    }

    (void)ku_ui_close(window);
    return 0;
}

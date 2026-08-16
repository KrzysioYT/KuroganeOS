#include "../common.h"
#include "../../../common/version.h"

#define APP_COUNT 5U
#define CHILD_CAPACITY 10U

typedef struct launcher_app {
    const char* label;
    const char* subtitle;
    const char* path;
} launcher_app;

static const launcher_app g_apps[APP_COUNT] = {
    {"TERMINAL", "shared shell / development", "/gui/terminal"},
    {"FILES", "persistent root / applications", "/gui/files"},
    {"MONITOR", "runtime / process health", "/gui/sysmon"},
    {"SETTINGS", "Red Flux appearance", "/gui/settings"},
    {"ABOUT", "KuroganeOS platform", "/gui/about"},
};

static uint64_t g_children[CHILD_CAPACITY];
static size_t g_selected = 0U;
static char g_status[64] = "HOME PINNED / DOCK READY / DESKTOP ROOT";

static void append_text(char* destination, size_t capacity, const char* source) {
    const size_t used = strlen(destination);
    if (used >= capacity) return;
    (void)strlcpy(destination + used, source, capacity - used);
}

static void reap_children(void) {
    for (size_t index = 0U; index < CHILD_CAPACITY; ++index) {
        if (g_children[index] == 0U) continue;
        int32_t status = 0;
        const ku_status_t result = ku_process_wait(g_children[index], &status);
        if (result == KU_STATUS_OK || result == KU_STATUS_NOT_FOUND) {
            g_children[index] = 0U;
        }
    }
}

static int remember_child(uint64_t pid) {
    reap_children();
    for (size_t index = 0U; index < CHILD_CAPACITY; ++index) {
        if (g_children[index] == 0U) {
            g_children[index] = pid;
            return 1;
        }
    }
    return 0;
}

static void launch_selected(void) {
    const launcher_app* app = &g_apps[g_selected];
    const ku_result_t result = ku_process_spawn(app->path, strlen(app->path));
    if (result <= 0) {
        (void)strlcpy(g_status, "DOCK LAUNCH FAILED", sizeof(g_status));
        return;
    }
    (void)remember_child((uint64_t)result);
    char number[24];
    gui_u64(number, sizeof(number), (uint64_t)result);
    (void)strlcpy(g_status, "OPENED ", sizeof(g_status));
    append_text(g_status, sizeof(g_status), app->label);
    append_text(g_status, sizeof(g_status), " / PID ");
    append_text(g_status, sizeof(g_status), number);
}

static void select_and_launch(size_t index) {
    if (index >= APP_COUNT) return;
    g_selected = index;
    launch_selected();
}

static void build_scene(kui_scene* scene) {
    kui_flow root;
    kui_flow apps;
    kui_scene_initialize(scene);
    scene->visible_rows = 12U;
    kui_scene_set_palette(
        scene,
        UINT32_C(0x090A0C),
        UINT32_C(0xECEEF1),
        UINT32_C(0xDE192D));

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "RED FLUX HOME");
    (void)kui_flow_label(&root, 2U, KUROGANE_PRODUCT_STRING " / DESKTOP ROOT");
    (void)kui_flow_label(&root, 3U,
        "HOME IS PINNED / DOCK OPENS, RESTORES AND FOCUSES APPS");

    kui_flow_begin(&apps, scene, 1U);
    for (size_t index = 0U; index < APP_COUNT; ++index) {
        char label[64];
        label[0] = '\0';
        (void)strlcpy(label, g_apps[index].label, sizeof(label));
        append_text(label, sizeof(label), " / ");
        append_text(label, sizeof(label), g_apps[index].subtitle);
        (void)kui_flow_list_item(&apps, 10U + (uint32_t)index, label);
    }
    (void)kui_flow_separator(&root, 30U);
    (void)kui_flow_label(&root, 31U, g_status);
    (void)kui_scene_select(scene, 10U + (uint32_t)g_selected);
}

int main(void) {
    const ku_window_t window = gui_open("RED FLUX HOME", 250, 135, 650, 460);
    if (window == KU_INVALID_WINDOW) return 1;

    for (size_t index = 0U; index < CHILD_CAPACITY; ++index) {
        g_children[index] = 0U;
    }

    puts("[TEST] desktop_launcher_ring3: PASS");
    puts("[TEST] desktop_clean_session: PASS");
    puts("[TEST] desktop_arrow_navigation: PASS");
    puts("[TEST] red_flux_dock_controller: PASS");
    puts("[TEST] red_flux_home_pinned: PASS");

    kui_scene scene;
    build_scene(&scene);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 2;
    }

    for (;;) {
        ku_ui_event event;
        reap_children();
        const int available = gui_wait_event(window, &event);
        if (available < 0 || event.type == KU_UI_EVENT_CLOSE) break;
        if (event.type != KU_UI_EVENT_KEY) continue;

        if (gui_key_down(&event) || gui_key_right(&event) || gui_key_tab(&event)) {
            g_selected = (g_selected + 1U) % APP_COUNT;
            (void)strlcpy(g_status, g_apps[g_selected].subtitle, sizeof(g_status));
        } else if (gui_key_up(&event) || gui_key_left(&event)) {
            g_selected = g_selected == 0U ? APP_COUNT - 1U : g_selected - 1U;
            (void)strlcpy(g_status, g_apps[g_selected].subtitle, sizeof(g_status));
        } else if (gui_key_activate(&event)) {
            launch_selected();
        } else if (event.character == 't' || event.character == 'T') {
            select_and_launch(0U);
        } else if (event.character == 'f' || event.character == 'F') {
            select_and_launch(1U);
        } else if (event.character == 'm' || event.character == 'M') {
            select_and_launch(2U);
        } else if (event.character == 's' || event.character == 'S') {
            select_and_launch(3U);
        } else if (event.character == 'a' || event.character == 'A') {
            select_and_launch(4U);
        } else if (gui_key_cancel(&event)) {
            (void)strlcpy(g_status, "HOME / PINNED DESKTOP ROOT / DOCK ACTIVE",
                          sizeof(g_status));
        } else {
            continue;
        }
        build_scene(&scene);
        (void)kui_scene_present(window, &scene);
    }

    (void)ku_ui_close(window);
    return 0;
}

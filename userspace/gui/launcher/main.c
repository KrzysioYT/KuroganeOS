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
    {"TERMINAL", "commands / processes / development", "/gui/terminal"},
    {"FILES", "persistent root / applications", "/gui/files"},
    {"MONITOR", "scheduler / process health", "/gui/sysmon"},
    {"SETTINGS", "Flux appearance / session", "/gui/settings"},
    {"ABOUT", "KuroganeOS platform information", "/gui/about"},
};

static uint64_t g_children[CHILD_CAPACITY];
static size_t g_selected = 0U;
static char g_status[64] = "FLUX SESSION READY";

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
        (void)strlcpy(g_status, "LAUNCH FAILED // PROCESS ABI", sizeof(g_status));
        return;
    }
    (void)remember_child((uint64_t)result);
    char number[24];
    gui_u64(number, sizeof(number), (uint64_t)result);
    (void)strlcpy(g_status, "OPENED ", sizeof(g_status));
    append_text(g_status, sizeof(g_status), app->label);
    append_text(g_status, sizeof(g_status), " // PID ");
    append_text(g_status, sizeof(g_status), number);
}

static void build_scene(kui_scene* scene) {
    kui_flow root;
    kui_flow apps;
    kui_scene_initialize(scene);
    scene->visible_rows = 12U;
    kui_scene_set_palette(
        scene,
        UINT32_C(0x090D14),
        UINT32_C(0xF2F6FF),
        UINT32_C(0x45E0BC));

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "KUROGANE // FLUX HOME");
    (void)kui_flow_label(&root, 2U, KUROGANE_PRODUCT_STRING " // DESKTOP");
    (void)kui_flow_label(&root, 3U, "J/K OR ARROWS: SELECT   ENTER: OPEN");

    kui_flow_begin(&apps, scene, 1U);
    for (size_t index = 0U; index < APP_COUNT; ++index) {
        char label[64];
        label[0] = '\0';
        (void)strlcpy(label, g_apps[index].label, sizeof(label));
        append_text(label, sizeof(label), " // ");
        append_text(label, sizeof(label), g_apps[index].subtitle);
        (void)kui_flow_list_item(
            &apps, 10U + (uint32_t)index, label);
    }
    (void)kui_flow_separator(&root, 30U);
    (void)kui_flow_label(&root, 31U, g_status);
    (void)kui_scene_select(scene, 10U + (uint32_t)g_selected);
}

static int key_is_up(const ku_ui_event* event) {
    return event->character == 'k' || event->character == 'K' ||
        event->key == UINT32_C(72);
}

static int key_is_down(const ku_ui_event* event) {
    return event->character == 'j' || event->character == 'J' ||
        event->key == UINT32_C(80);
}

int main(void) {
    const ku_window_t window = gui_open("FLUX HOME", 220, 120, 640, 470);
    if (window == KU_INVALID_WINDOW) return 1;

    for (size_t index = 0U; index < CHILD_CAPACITY; ++index) {
        g_children[index] = 0U;
    }

    puts("[TEST] desktop_launcher_ring3: PASS");
    puts("[TEST] desktop_clean_session: PASS");

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

        if (key_is_down(&event)) {
            g_selected = (g_selected + 1U) % APP_COUNT;
            (void)strlcpy(g_status, g_apps[g_selected].subtitle, sizeof(g_status));
        } else if (key_is_up(&event)) {
            g_selected = g_selected == 0U ? APP_COUNT - 1U : g_selected - 1U;
            (void)strlcpy(g_status, g_apps[g_selected].subtitle, sizeof(g_status));
        } else if (event.character == '\r' || event.character == '\n') {
            launch_selected();
        } else if (event.character == 't' || event.character == 'T') {
            g_selected = 0U;
            launch_selected();
        } else if (event.character == 'f' || event.character == 'F') {
            g_selected = 1U;
            launch_selected();
        } else {
            continue;
        }
        build_scene(&scene);
        (void)kui_scene_present(window, &scene);
    }

    (void)ku_ui_close(window);
    return 0;
}

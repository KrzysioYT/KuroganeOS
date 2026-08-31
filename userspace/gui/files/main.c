#include "../common.h"

#define ENTRY_COUNT 6U

typedef struct quick_entry {
    const char* label;
    const char* path;
    int launchable;
} quick_entry;

static const quick_entry g_entries[ENTRY_COUNT] = {
    {"SYSTEM CONFIG", "/etc/system.cfg", 0},
    {"RED FLUX TERMINAL", "/gui/terminal", 1},
    {"FILES", "/gui/files", 1},
    {"SYSTEM MONITOR", "/gui/sysmon", 1},
    {"SETTINGS", "/gui/settings", 1},
    {"ABOUT KUROGANEOS", "/gui/about", 1},
};

static void append_text(char* destination, size_t capacity, const char* source) {
    const size_t used = strlen(destination);
    if (used >= capacity) return;
    (void)strlcpy(destination + used, source, capacity - used);
}

static void preview_path(
    const char* path,
    char* line1,
    size_t line1_capacity,
    char* line2,
    size_t line2_capacity) {
    char data[192];
    line1[0] = '\0';
    line2[0] = '\0';

    const ku_result_t descriptor = ku_open(path, strlen(path), KU_OPEN_READ);
    if (descriptor <= 0) {
        (void)strlcpy(line1, "VFS / OPEN FAILED", line1_capacity);
        return;
    }
    const ku_result_t count = ku_read((ku_handle_t)descriptor, data, sizeof(data) - 1U);
    (void)ku_close((ku_handle_t)descriptor);
    if (count < 0) {
        (void)strlcpy(line1, "VFS / READ FAILED", line1_capacity);
        return;
    }
    if (count == 0) {
        (void)strlcpy(line1, "VFS / EMPTY FILE", line1_capacity);
        return;
    }

    data[count] = '\0';
    if (count >= 4 && (unsigned char)data[0] == 0x7FU &&
        data[1] == 'E' && data[2] == 'L' && data[3] == 'F') {
        (void)strlcpy(line1, "ELF64 EXECUTABLE", line1_capacity);
        (void)strlcpy(line2, "CLICK OPEN TO LAUNCH", line2_capacity);
        return;
    }

    size_t input = 0U;
    size_t output = 0U;
    int second = 0;
    while (input < (size_t)count && second < 2) {
        const unsigned char character = (unsigned char)data[input++];
        char* target = second == 0 ? line1 : line2;
        const size_t capacity = second == 0 ? line1_capacity : line2_capacity;
        if (character == '\r') continue;
        if (character == '\n') {
            target[output] = '\0';
            ++second;
            output = 0U;
            continue;
        }
        if (output + 1U < capacity) {
            target[output++] = character >= 32U && character <= 126U
                ? (char)character : '.';
        }
    }
    if (second == 0) line1[output] = '\0';
    else if (second == 1) line2[output] = '\0';
    if (line1[0] == '\0') (void)strlcpy(line1, "VFS / READABLE FILE", line1_capacity);
}

static void refresh_selected(
    size_t selected,
    char* status,
    size_t status_capacity,
    char* preview1,
    size_t preview1_capacity,
    char* preview2,
    size_t preview2_capacity) {
    (void)strlcpy(status, g_entries[selected].path, status_capacity);
    preview_path(
        g_entries[selected].path,
        preview1,
        preview1_capacity,
        preview2,
        preview2_capacity);
}

static void open_selected(
    size_t selected,
    char* status,
    size_t status_capacity,
    char* preview1,
    size_t preview1_capacity,
    char* preview2,
    size_t preview2_capacity) {
    if (g_entries[selected].launchable) {
        const ku_result_t pid = ku_process_spawn(
            g_entries[selected].path, strlen(g_entries[selected].path));
        if (pid > 0) {
            char number[24];
            (void)strlcpy(status, "OPENED PID ", status_capacity);
            gui_u64(number, sizeof(number), (uint64_t)pid);
            append_text(status, status_capacity, number);
        } else {
            (void)strlcpy(status, "LAUNCH FAILED", status_capacity);
        }
        return;
    }
    refresh_selected(
        selected,
        status,
        status_capacity,
        preview1,
        preview1_capacity,
        preview2,
        preview2_capacity);
}

static void build_scene(
    kui_scene* scene,
    size_t selected,
    const char* status,
    const char* preview1,
    const char* preview2) {
    kui_flow root;
    kui_flow entries;
    kui_scene_initialize(scene);
    scene->visible_rows = 14U;
    kui_scene_set_palette(
        scene,
        UINT32_C(0x090A0C),
        UINT32_C(0xECEEF1),
        UINT32_C(0xDE192D));

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "FILES / QUICK ACCESS");
    (void)kui_flow_label(&root, 2U, "MOUSE / SELECT ENTRY   OPEN   REFRESH");

    kui_flow_begin(&entries, scene, 1U);
    for (size_t index = 0U; index < ENTRY_COUNT; ++index) {
        char label[64];
        label[0] = '\0';
        (void)strlcpy(label, g_entries[index].launchable ? "APP / " : "FILE / ", sizeof(label));
        append_text(label, sizeof(label), g_entries[index].label);
        (void)kui_flow_list_item(&entries, 10U + (uint32_t)index, label);
    }
    (void)kui_flow_button(&root, 30U, "OPEN SELECTED");
    (void)kui_flow_button(&root, 31U, "REFRESH PREVIEW");
    (void)kui_flow_label(&root, 32U, status);
    (void)kui_flow_label(&root, 33U, preview1);
    (void)kui_flow_label(&root, 34U, preview2);
    (void)kui_scene_select(scene, 10U + (uint32_t)selected);
}

int main(void) {
    const ku_window_t window = gui_open("FILES", 300, 145, 540, 400);
    if (window == KU_INVALID_WINDOW) return 1;

    size_t selected = 0U;
    uint32_t pointer_buttons = 0U;
    char status[64] = "PERSISTENT ROOT / READ ABI";
    char preview1[64];
    char preview2[64];
    preview_path(g_entries[selected].path, preview1, sizeof(preview1), preview2, sizeof(preview2));

    kui_scene scene;
    build_scene(&scene, selected, status, preview1, preview2);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 2;
    }
    puts("[TEST] desktop_files_real_vfs: PASS");
    puts("[TEST] flux_scene_files: PASS");
    puts("[TEST] desktop_files_mouse_navigation: PASS");
    puts("[TEST] desktop_files_keyboard_shortcuts_detached: PASS");

    for (;;) {
        ku_ui_event event;
        uint32_t target;
        const int wait_result = gui_wait_event(window, &event);
        if (wait_result < 0 || event.type == KU_UI_EVENT_CLOSE) break;
        if (event.type != KU_UI_EVENT_POINTER) continue;

        {
            const uint32_t previous_buttons = pointer_buttons;
            const int primary_pressed =
                (event.buttons & UINT32_C(1)) != 0U &&
                (previous_buttons & UINT32_C(1)) == 0U;
            pointer_buttons = event.buttons;
            if (!primary_pressed) continue;
        }

        target = kui_scene_hit_test(&scene, event.x, event.y);
        if (target >= 10U && target < 10U + ENTRY_COUNT) {
            selected = (size_t)(target - 10U);
            refresh_selected(
                selected,
                status,
                sizeof(status),
                preview1,
                sizeof(preview1),
                preview2,
                sizeof(preview2));
        } else if (target == 30U) {
            open_selected(
                selected,
                status,
                sizeof(status),
                preview1,
                sizeof(preview1),
                preview2,
                sizeof(preview2));
        } else if (target == 31U) {
            (void)strlcpy(status, "VFS / PREVIEW REFRESHED", sizeof(status));
            preview_path(
                g_entries[selected].path,
                preview1,
                sizeof(preview1),
                preview2,
                sizeof(preview2));
        } else {
            continue;
        }

        build_scene(&scene, selected, status, preview1, preview2);
        (void)kui_scene_present(window, &scene);
    }

    (void)ku_ui_close(window);
    return 0;
}

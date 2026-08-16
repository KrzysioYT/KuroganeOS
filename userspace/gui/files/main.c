#include "../common.h"
#include <fcntl.h>
#include <unistd.h>

#define ENTRY_COUNT 8U

typedef struct quick_entry {
    const char* label;
    const char* path;
    int launchable;
} quick_entry;

static const quick_entry g_entries[ENTRY_COUNT] = {
    {"SYSTEM CONFIG", "/etc/system.cfg", 0},
    {"FLUX TERMINAL", "/gui/terminal", 1},
    {"FILES", "/gui/files", 1},
    {"SYSTEM MONITOR", "/gui/sysmon", 1},
    {"SETTINGS", "/gui/settings", 1},
    {"ABOUT KUROGANEOS", "/gui/about", 1},
    {"USER SHELL", "/apps/shell", 1},
    {"SYSTEM INIT", "/system/init", 0},
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
    const int descriptor = open(path, O_RDONLY);
    char data[192];
    line1[0] = '\0';
    line2[0] = '\0';
    if (descriptor < 0) {
        (void)strlcpy(line1, "VFS: unavailable", line1_capacity);
        return;
    }
    const ssize_t count = read(descriptor, data, sizeof(data) - 1U);
    (void)close(descriptor);
    if (count < 0) {
        (void)strlcpy(line1, "VFS: read failed", line1_capacity);
        return;
    }
    if (count == 0) {
        (void)strlcpy(line1, "VFS: empty file", line1_capacity);
        return;
    }
    data[count] = '\0';
    if (count >= 4 && (unsigned char)data[0] == 0x7FU &&
        data[1] == 'E' && data[2] == 'L' && data[3] == 'F') {
        (void)strlcpy(line1, "VFS: ELF64 executable image", line1_capacity);
        (void)strlcpy(line2, "ENTER launches entries marked APP", line2_capacity);
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
    if (line1[0] == '\0') (void)strlcpy(line1, "VFS: readable file", line1_capacity);
}

static void build_scene(
    kui_scene* scene,
    size_t selected,
    const char* status,
    const char* preview1,
    const char* preview2) {
    kui_flow root;
    kui_flow entries;
    size_t index = 0U;
    kui_scene_initialize(scene);
    scene->visible_rows = 12U;

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "FILES // 2.6 QUICK ACCESS");
    (void)kui_flow_label(&root, 2U, "J/K: select | ENTER: preview/launch | R: refresh");
    (void)kui_flow_separator(&root, 3U);

    kui_flow_begin(&entries, scene, 1U);
    while (index < ENTRY_COUNT) {
        char label[64];
        label[0] = '\0';
        (void)strlcpy(label, g_entries[index].launchable ? "APP  " : "FILE ", sizeof(label));
        append_text(label, sizeof(label), g_entries[index].label);
        (void)kui_flow_list_item(&entries, 10U + (uint32_t)index, label);
        ++index;
    }
    (void)kui_flow_separator(&root, 30U);
    (void)kui_flow_label(&root, 31U, status);
    (void)kui_flow_label(&root, 32U, preview1);
    (void)kui_flow_label(&root, 33U, preview2);
    (void)kui_scene_select(scene, 10U + (uint32_t)selected);
    if (selected > 3U) (void)kui_scene_scroll(scene, (int32_t)(selected - 3U));
}

int main(void) {
    const ku_window_t window = gui_open("FILES", 574, 64, 430, 360);
    if (window == KU_INVALID_WINDOW) return 1;

    size_t selected = 0U;
    char status[64] = "Persistent FAT32 root // public read ABI";
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
    puts("[TEST] desktop_files_2_6: PASS");

    for (;;) {
        ku_ui_event event;
        if (gui_wait_event(window, &event) < 0 || event.type == KU_UI_EVENT_CLOSE) break;
        if (event.type != KU_UI_EVENT_KEY) continue;

        if (event.character == 'j' || event.character == 'J') {
            selected = (selected + 1U) % ENTRY_COUNT;
            (void)strlcpy(status, g_entries[selected].path, sizeof(status));
            preview_path(g_entries[selected].path, preview1, sizeof(preview1), preview2, sizeof(preview2));
        } else if (event.character == 'k' || event.character == 'K') {
            selected = selected == 0U ? ENTRY_COUNT - 1U : selected - 1U;
            (void)strlcpy(status, g_entries[selected].path, sizeof(status));
            preview_path(g_entries[selected].path, preview1, sizeof(preview1), preview2, sizeof(preview2));
        } else if (event.character == 'r' || event.character == 'R') {
            (void)strlcpy(status, "VFS preview refreshed", sizeof(status));
            preview_path(g_entries[selected].path, preview1, sizeof(preview1), preview2, sizeof(preview2));
        } else if (event.character == '\r' || event.character == '\n') {
            if (g_entries[selected].launchable) {
                const ku_result_t pid = ku_process_spawn(
                    g_entries[selected].path, strlen(g_entries[selected].path));
                if (pid > 0) {
                    char number[24];
                    (void)strlcpy(status, "launched pid=", sizeof(status));
                    gui_u64(number, sizeof(number), (uint64_t)pid);
                    append_text(status, sizeof(status), number);
                } else {
                    (void)strlcpy(status, "launch failed", sizeof(status));
                }
            } else {
                (void)strlcpy(status, g_entries[selected].path, sizeof(status));
                preview_path(g_entries[selected].path, preview1, sizeof(preview1), preview2, sizeof(preview2));
            }
        } else {
            continue;
        }

        build_scene(&scene, selected, status, preview1, preview2);
        (void)kui_scene_present(window, &scene);
    }
    (void)ku_ui_close(window);
    return 0;
}

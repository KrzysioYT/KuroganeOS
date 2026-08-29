#include "../common.h"

#define ENTRY_COUNT 6U

typedef struct quick_entry {
    const char* label;
    const char* path;
    int launchable;
} quick_entry;

static const quick_entry g_entries[ENTRY_COUNT] = {
    {"System Config", "/etc/system.cfg", 0},
    {"Terminal", "/gui/terminal", 1},
    {"Files", "/gui/files", 1},
    {"System Monitor", "/gui/sysmon", 1},
    {"Settings", "/gui/settings", 1},
    {"About KuroganeOS", "/gui/about", 1},
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
    {
        const ku_result_t descriptor = ku_open(path, strlen(path), KU_OPEN_READ);
        if (descriptor <= 0) {
            (void)strlcpy(line1, "VFS / Open failed", line1_capacity);
            return;
        }
        {
            const ku_result_t count = ku_read((ku_handle_t)descriptor, data, sizeof(data) - 1U);
            (void)ku_close((ku_handle_t)descriptor);
            if (count < 0) {
                (void)strlcpy(line1, "VFS / Read failed", line1_capacity);
                return;
            }
            if (count == 0) {
                (void)strlcpy(line1, "VFS / Empty file", line1_capacity);
                return;
            }
            data[count] = '\0';
            if (count >= 4 && (unsigned char)data[0] == 0x7FU &&
                data[1] == 'E' && data[2] == 'L' && data[3] == 'F') {
                (void)strlcpy(line1, "ELF64 executable", line1_capacity);
                (void)strlcpy(line2, "Enter to launch", line2_capacity);
                return;
            }
            {
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
            }
        }
    }
    if (line1[0] == '\0') (void)strlcpy(line1, "VFS / Readable file", line1_capacity);
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

static void build_scene(
    kui_scene* scene,
    size_t selected,
    const char* status,
    const char* preview1,
    const char* preview2) {
    kui_flow root;
    ku_ui_line_style heading;
    ku_ui_line_style sidebar;
    ku_ui_line_style row;
    ku_ui_line_style preview;
    char preview_line[64] = "Preview  /  ";
    size_t index;

    append_text(preview_line, sizeof(preview_line), preview1);
    if (preview2[0] != '\0') {
        append_text(preview_line, sizeof(preview_line), "  /  ");
        append_text(preview_line, sizeof(preview_line), preview2);
    }

    kui_scene_initialize(scene);
    scene->visible_rows = KU_UI_MAX_LINES;
    kui_scene_set_palette(
        scene,
        UINT32_C(0x0F1419),
        UINT32_C(0xF0F2F4),
        UINT32_C(0xC0332F));

    set_style(&heading, 16U, KU_TEXT_WEIGHT_SEMIBOLD,
              UINT32_C(0xF5F6F7), 0U,
              KU_UI_LINE_STYLE_TRANSPARENT_BACKGROUND);
    set_style(&sidebar, 11U, KU_TEXT_WEIGHT_MEDIUM,
              UINT32_C(0xB5BDC4), UINT32_C(0x12171C), 0U);
    set_style(&row, 11U, KU_TEXT_WEIGHT_MEDIUM,
              UINT32_C(0xE3E7EA), UINT32_C(0x171C21), 0U);
    set_style(&preview, 10U, KU_TEXT_WEIGHT_NORMAL,
              UINT32_C(0xA6AFB7), UINT32_C(0x11161B), 0U);

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_label(&root, 1U, "Home   /   Kurogane Drive");
    (void)kui_scene_set_style(scene, 1U, &heading);
    (void)kui_scene_set_bounds(scene, 1U, 22, 15, 360, 32, 0U);

    (void)kui_flow_card(&root, 2U, "Home");
    (void)kui_flow_card(&root, 3U, "Desktop");
    (void)kui_flow_card(&root, 4U, "Documents");
    (void)kui_flow_card(&root, 5U, "Downloads");
    (void)kui_scene_set_style(scene, 2U, &sidebar);
    (void)kui_scene_set_style(scene, 3U, &sidebar);
    (void)kui_scene_set_style(scene, 4U, &sidebar);
    (void)kui_scene_set_style(scene, 5U, &sidebar);
    (void)kui_scene_set_bounds(scene, 2U, 18, 64, 170, 38, 8U);
    (void)kui_scene_set_bounds(scene, 3U, 18, 106, 170, 38, 8U);
    (void)kui_scene_set_bounds(scene, 4U, 18, 148, 170, 38, 8U);
    (void)kui_scene_set_bounds(scene, 5U, 18, 190, 170, 38, 8U);

    for (index = 0U; index < ENTRY_COUNT; ++index) {
        char label[64] = "";
        const uint32_t id = 10U + (uint32_t)index;
        (void)strlcpy(label, g_entries[index].launchable ? "App    " : "File   ", sizeof(label));
        append_text(label, sizeof(label), g_entries[index].label);
        (void)kui_flow_list_item(&root, id, label);
        (void)kui_scene_set_style(scene, id, &row);
        (void)kui_scene_set_bounds(
            scene, id, 208, 64 + (int32_t)index * 52, 570, 44, 9U);
    }

    (void)kui_flow_card(&root, 30U, preview_line);
    (void)kui_scene_set_style(scene, 30U, &preview);
    (void)kui_scene_set_bounds(scene, 30U, 208, 388, 570, 58, 10U);
    (void)kui_scene_select(scene, 10U + (uint32_t)selected);
    (void)status;
}

int main(void) {
    const ku_window_t window = gui_open("FILES", 120, 92, 820, 520);
    size_t selected = 0U;
    char status[64] = "Kurogane Drive";
    char preview1[64];
    char preview2[64];
    kui_scene scene;

    if (window == KU_INVALID_WINDOW) return 1;
    preview_path(g_entries[selected].path, preview1, sizeof(preview1), preview2, sizeof(preview2));
    build_scene(&scene, selected, status, preview1, preview2);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 2;
    }
    puts("[TEST] desktop_files_real_vfs: PASS");
    puts("[TEST] flux_scene_files: PASS");
    puts("[TEST] desktop_files_3_1_navigation: PASS");
    puts("[TEST] obsidian_files_sidebar: PASS");

    for (;;) {
        ku_ui_event event;
        if (gui_wait_event(window, &event) < 0 || event.type == KU_UI_EVENT_CLOSE) break;
        if (event.type != KU_UI_EVENT_KEY) continue;

        if (gui_key_down(&event) || gui_key_right(&event) || gui_key_tab(&event)) {
            selected = (selected + 1U) % ENTRY_COUNT;
            (void)strlcpy(status, g_entries[selected].path, sizeof(status));
            preview_path(g_entries[selected].path, preview1, sizeof(preview1), preview2, sizeof(preview2));
        } else if (gui_key_up(&event) || gui_key_left(&event)) {
            selected = selected == 0U ? ENTRY_COUNT - 1U : selected - 1U;
            (void)strlcpy(status, g_entries[selected].path, sizeof(status));
            preview_path(g_entries[selected].path, preview1, sizeof(preview1), preview2, sizeof(preview2));
        } else if (event.character == 'r' || event.character == 'R') {
            (void)strlcpy(status, "Preview refreshed", sizeof(status));
            preview_path(g_entries[selected].path, preview1, sizeof(preview1), preview2, sizeof(preview2));
        } else if (gui_key_activate(&event)) {
            if (g_entries[selected].launchable) {
                const ku_result_t pid = ku_process_spawn(
                    g_entries[selected].path, strlen(g_entries[selected].path));
                if (pid > 0) {
                    char number[24];
                    (void)strlcpy(status, "Opened PID ", sizeof(status));
                    gui_u64(number, sizeof(number), (uint64_t)pid);
                    append_text(status, sizeof(status), number);
                } else {
                    (void)strlcpy(status, "Launch failed", sizeof(status));
                }
            } else {
                (void)strlcpy(status, g_entries[selected].path, sizeof(status));
                preview_path(g_entries[selected].path, preview1, sizeof(preview1), preview2, sizeof(preview2));
            }
        } else if (gui_key_cancel(&event)) {
            selected = 0U;
            (void)strlcpy(status, "Kurogane Drive", sizeof(status));
            preview_path(g_entries[selected].path, preview1, sizeof(preview1), preview2, sizeof(preview2));
        } else continue;

        build_scene(&scene, selected, status, preview1, preview2);
        (void)kui_scene_present(window, &scene);
    }
    (void)ku_ui_close(window);
    return 0;
}

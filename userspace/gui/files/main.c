#include "../common.h"
#include <fcntl.h>
#include <unistd.h>

static void build_scene(kui_scene* scene, const char* config_line) {
    kui_flow root;
    kui_flow entries;
    kui_scene_initialize(scene);
    scene->visible_rows = 10U;

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "FILES // PERSISTENT ROOT");
    (void)kui_flow_label(&root, 2U, "J/K: move + scroll | ENTER: reserved for 2.6");
    (void)kui_flow_separator(&root, 3U);

    kui_flow_begin(&entries, scene, 1U);
    (void)kui_flow_list_item(&entries, 10U, "/apps/");
    (void)kui_flow_list_item(&entries, 11U, "/gui/");
    (void)kui_flow_list_item(&entries, 12U, "/system/");
    (void)kui_flow_list_item(&entries, 13U, "/etc/");
    (void)kui_flow_list_item(&entries, 14U, "/var/");
    (void)kui_flow_list_item(&entries, 15U, "/EFI/");
    (void)kui_flow_separator(&entries, 16U);
    (void)kui_flow_label(&entries, 17U, "/etc/system.cfg");
    (void)kui_flow_label(&entries, 18U, config_line);
    (void)kui_flow_label(&entries, 19U, "Read through libc open/read/close.");
    (void)kui_flow_label(&entries, 20U, "Native readdir/stat navigation lands in 2.6.");
    (void)kui_scene_select(scene, 10U);
}

int main(void) {
    const ku_window_t window = gui_open("FILES", 565, 70, 400, 310);
    if (window == KU_INVALID_WINDOW) return 1;

    char config_line[128] = "Cannot open system configuration";
    const kuro_fd_t file = open("/etc/system.cfg", O_RDONLY);
    if (file >= 0) {
        const ssize_t count = read(file, config_line, sizeof(config_line) - 1U);
        (void)close(file);
        if (count > 0) {
            char* newline;
            config_line[count] = '\0';
            newline = config_line;
            while (*newline != '\0' && *newline != '\r' && *newline != '\n') ++newline;
            *newline = '\0';
            puts("[TEST] desktop_files_real_vfs: PASS");
        }
    }

    kui_scene scene;
    build_scene(&scene, config_line);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 2;
    }
    puts("[TEST] flux_scene_files: PASS");

    for (;;) {
        ku_ui_event event;
        if (gui_wait_event(window, &event) < 0 || event.type == KU_UI_EVENT_CLOSE) break;
        if (event.type != KU_UI_EVENT_KEY) continue;
        if (event.character == 'j' || event.character == 'J') {
            (void)kui_scene_select_next(&scene, 1);
            (void)kui_scene_scroll(&scene, 1);
            (void)kui_scene_present(window, &scene);
        } else if (event.character == 'k' || event.character == 'K') {
            (void)kui_scene_select_next(&scene, -1);
            (void)kui_scene_scroll(&scene, -1);
            (void)kui_scene_present(window, &scene);
        }
    }
    (void)ku_ui_close(window);
    return 0;
}

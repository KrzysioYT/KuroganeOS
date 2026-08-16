#include "../common.h"
#include <fcntl.h>
#include <unistd.h>

int main(void) {
    const ku_window_t window = gui_open("FILES", 565, 70, 400, 270);
    if (window == KU_INVALID_WINDOW) return 1;
    ku_ui_frame frame;
    kui_frame_initialize(&frame);
    (void)kui_frame_set_line(&frame, 0U, "FILES - /etc/system.cfg");
    (void)kui_frame_set_line(&frame, 1U, "/");
    (void)kui_frame_set_line(&frame, 2U, "  apps/     gui/      system/");
    (void)kui_frame_set_line(&frame, 3U, "  etc/      var/      EFI/");
    const kuro_fd_t file = open("/etc/system.cfg", O_RDONLY);
    char data[128] = {0};
    if (file >= 0) {
        const ssize_t count = read(file, data, sizeof(data) - 1U);
        (void)close(file);
        if (count > 0) {
            data[count] = '\0';
            char* newline = data;
            while (*newline != '\0' && *newline != '\r' && *newline != '\n') ++newline;
            *newline = '\0';
            (void)kui_frame_set_line(&frame, 5U, data);
            puts("[TEST] desktop_files_real_vfs: PASS");
        }
    } else {
        (void)kui_frame_set_line(&frame, 5U, "Cannot open system configuration");
    }
    (void)kui_frame_set_line(&frame, 7U, "Read through libc open/read/close.");
    (void)kui_present(window, &frame);
    for (;;) {
        ku_ui_event event;
        if (gui_wait_event(window, &event) < 0 ||
            event.type == KU_UI_EVENT_CLOSE) break;
    }
    (void)ku_ui_close(window);
    return 0;
}

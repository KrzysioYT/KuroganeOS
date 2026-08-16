#include "../common.h"
#include "../../../common/version.h"

int main(void) {
    const ku_window_t window = gui_open("ABOUT KUROGANEOS", 280, 175, 470, 260);
    if (window == KU_INVALID_WINDOW) return 1;
    ku_ui_frame frame;
    kui_frame_initialize(&frame);
    (void)kui_frame_set_line(&frame, 0U, "KUROGANEOS " KUROGANE_VERSION_STRING " FLUX WINDOW CORE");
    (void)kui_frame_set_line(&frame, 2U, "x86-64 UEFI, private address spaces");
    (void)kui_frame_set_line(&frame, 3U, "Preemptive tasks, writable FAT32, AHCI");
    (void)kui_frame_set_line(&frame, 4U, "Flux session, PID 1 userspace, WindowManager");
    (void)kui_frame_set_line(&frame, 6U, "This application was linked with libui.");
    (void)kui_present(window, &frame);
    puts("[TEST] desktop_about_ring3: PASS");
    for (;;) {
        ku_ui_event event;
        if (gui_wait_event(window, &event) < 0 ||
            event.type == KU_UI_EVENT_CLOSE) break;
    }
    (void)ku_ui_close(window);
    return 0;
}

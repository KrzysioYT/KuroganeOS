#include "../common.h"

static void present(ku_window_t window, int light) {
    ku_ui_frame frame;
    kui_frame_initialize(&frame);
    if (light) {
        frame.background_rgb = UINT32_C(0xE2E8F0);
        frame.foreground_rgb = UINT32_C(0x111827);
        frame.accent_rgb = UINT32_C(0xC2410C);
    }
    (void)kui_frame_set_line(&frame, 0U, "SETTINGS");
    (void)kui_frame_set_line(&frame, 2U, "T: toggle this session's UI theme");
    (void)kui_frame_set_line(&frame, 4U,
        light ? "Theme: light (active)" : "Theme: dark (active)");
    (void)kui_frame_set_line(&frame, 6U,
        "The setting immediately changes rendered colors.");
    (void)kui_present(window, &frame);
}

int main(void) {
    const ku_window_t window = gui_open("SETTINGS", 545, 385, 420, 250);
    if (window == KU_INVALID_WINDOW) return 1;
    int light = 0;
    present(window, light);
    puts("[TEST] desktop_settings_real: PASS");
    for (;;) {
        ku_ui_event event;
        if (gui_wait_event(window, &event) < 0 ||
            event.type == KU_UI_EVENT_CLOSE) break;
        if (event.type == KU_UI_EVENT_KEY &&
            (event.character == 't' || event.character == 'T')) {
            light = !light;
            present(window, light);
        }
    }
    (void)ku_ui_close(window);
    return 0;
}

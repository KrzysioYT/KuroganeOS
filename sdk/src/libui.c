#include <kurogane/libui.h>
#include <string.h>

void kui_frame_initialize(ku_ui_frame* frame) {
    if (frame == (ku_ui_frame*)0) return;
    memset(frame, 0, sizeof(*frame));
    frame->structure_size = sizeof(*frame);
    frame->background_rgb = UINT32_C(0x111827);
    frame->foreground_rgb = UINT32_C(0xE5E7EB);
    frame->accent_rgb = UINT32_C(0xF97316);
}

ku_status_t kui_frame_set_line(
    ku_ui_frame* frame, uint32_t line, const char* text) {
    if (frame == (ku_ui_frame*)0 || text == (const char*)0 ||
        line >= KU_UI_MAX_LINES) return KU_STATUS_INVALID_ARGUMENT;
    if (strlcpy(frame->lines[line], text, KU_UI_LINE_CAPACITY) >=
        KU_UI_LINE_CAPACITY) return KU_STATUS_OUT_OF_RANGE;
    if (frame->line_count <= line) frame->line_count = line + 1U;
    return KU_STATUS_OK;
}

ku_status_t kui_present(ku_window_t window, const ku_ui_frame* frame) {
    return ku_ui_present(window, frame);
}

int kui_next_event(ku_window_t window, ku_ui_event* event) {
    const ku_status_t status = ku_ui_poll(window, event);
    if (status == KU_STATUS_WOULD_BLOCK) return 0;
    return status == KU_STATUS_OK ? 1 : -1;
}

#ifndef KUROGANE_LIBUI_H
#define KUROGANE_LIBUI_H

#include <kurogane/ui.h>

#ifdef __cplusplus
extern "C" {
#endif

void kui_frame_initialize(ku_ui_frame* frame);
ku_status_t kui_frame_set_line(
    ku_ui_frame* frame, uint32_t line, const char* text);
ku_status_t kui_present(ku_window_t window, const ku_ui_frame* frame);
int kui_next_event(ku_window_t window, ku_ui_event* event);

#ifdef __cplusplus
}
#endif
#endif

#include "../../runtime/user.h"
#include <kurogane/ui.h>

__attribute__((noreturn)) void _start(void) {
    static const char title[] = "FLUX SURFACE WORKER";
    ku_ui_window_options options = {
        sizeof(ku_ui_window_options), 96, 108, 300, 220
    };
    ku_ui_frame frame = {0};
    ku_result_t created;

    created = ku_ui_create(title, sizeof(title) - 1U, &options);
    if (created <= 0) ku_exit(1);

    frame.structure_size = sizeof(ku_ui_frame);
    frame.background_rgb = UINT32_C(0x111317);
    frame.foreground_rgb = UINT32_C(0xECEEF1);
    frame.accent_rgb = UINT32_C(0xDE192D);
    frame.line_count = 1U;
    frame.lines[0][0] = 'F';
    frame.lines[0][1] = 'L';
    frame.lines[0][2] = 'U';
    frame.lines[0][3] = 'X';
    frame.lines[0][4] = '\0';

    if (ku_ui_present((ku_window_t)created, &frame) != KU_STATUS_OK) ku_exit(2);

    /* Deliberately leave the window open. Process retirement must release it. */
    ku_exit(0);
}

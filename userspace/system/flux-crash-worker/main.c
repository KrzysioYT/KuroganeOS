#include "../../runtime/user.h"
#include <kurogane/ui.h>

__attribute__((noreturn)) void _start(void) {
    static const char title[] = "FLUX CRASH WORKER";
    ku_ui_window_options options = {
        sizeof(ku_ui_window_options), 128, 132, 300, 220
    };
    ku_ui_frame frame = {0};
    ku_result_t created;
    volatile uintptr_t fault_address = 0U;

    created = ku_ui_create(title, sizeof(title) - 1U, &options);
    if (created <= 0) ku_exit(1);

    frame.structure_size = sizeof(ku_ui_frame);
    frame.background_rgb = UINT32_C(0x111317);
    frame.foreground_rgb = UINT32_C(0xECEEF1);
    frame.accent_rgb = UINT32_C(0xDE192D);
    frame.line_count = 1U;
    frame.lines[0][0] = 'C';
    frame.lines[0][1] = 'R';
    frame.lines[0][2] = 'A';
    frame.lines[0][3] = 'S';
    frame.lines[0][4] = 'H';
    frame.lines[0][5] = '\0';

    if (ku_ui_present((ku_window_t)created, &frame) != KU_STATUS_OK) ku_exit(2);

    /* Deliberate userspace page fault after owning a retained GUI surface. */
    *(volatile uint64_t*)fault_address = UINT64_C(0x4b55524f);
    ku_exit(3);
}

#ifndef KUROGANE_GUI_COMMON_H
#define KUROGANE_GUI_COMMON_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <kurogane/libui.h>
#include <kurogane/kurogane.h>

static inline void gui_u64(char* output, size_t capacity, uint64_t value) {
    char reverse[24];
    size_t count = 0;
    do {
        reverse[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && count < sizeof(reverse));
    size_t written = 0;
    while (count != 0U && written + 1U < capacity) {
        output[written++] = reverse[--count];
    }
    if (capacity != 0U) output[written] = '\0';
}

static inline ku_window_t gui_open(
    const char* title, int x, int y, int width, int height) {
    ku_ui_window_options options = {
        sizeof(ku_ui_window_options), x, y, width, height
    };
    const ku_result_t result = ku_ui_create(title, strlen(title), &options);
    return result > 0 ? (ku_window_t)result : KU_INVALID_WINDOW;
}

static inline int gui_wait_event(ku_window_t window, ku_ui_event* event) {
    for (;;) {
        const int available = kui_next_event(window, event);
        if (available != 0) return available;
        (void)kuro_sleep(1U);
    }
}

#endif

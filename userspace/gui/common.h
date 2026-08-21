#ifndef KUROGANE_GUI_COMMON_H
#define KUROGANE_GUI_COMMON_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <kurogane/libui.h>
#include <kurogane/kurogane.h>

#include "theme.h"

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

static inline void gui_append_text(
    char* destination, size_t capacity, const char* source) {
    size_t used;
    if (destination == (char*)0 || source == (const char*)0 || capacity == 0U) return;
    used = strlen(destination);
    if (used >= capacity) return;
    (void)strlcpy(destination + used, source, capacity - used);
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

static inline int gui_key_up(const ku_ui_event* event) {
    return event != NULL &&
        (event->key == KU_UI_KEY_ARROW_UP ||
         event->character == 'k' || event->character == 'K');
}

static inline int gui_key_down(const ku_ui_event* event) {
    return event != NULL &&
        (event->key == KU_UI_KEY_ARROW_DOWN ||
         event->character == 'j' || event->character == 'J');
}

static inline int gui_key_left(const ku_ui_event* event) {
    return event != NULL && event->key == KU_UI_KEY_ARROW_LEFT;
}

static inline int gui_key_right(const ku_ui_event* event) {
    return event != NULL && event->key == KU_UI_KEY_ARROW_RIGHT;
}

static inline int gui_key_activate(const ku_ui_event* event) {
    return event != NULL &&
        (event->key == KU_UI_KEY_ENTER ||
         event->character == '\r' || event->character == '\n');
}

static inline int gui_key_cancel(const ku_ui_event* event) {
    return event != NULL && event->key == KU_UI_KEY_ESCAPE;
}

static inline int gui_key_tab(const ku_ui_event* event) {
    return event != NULL && event->key == KU_UI_KEY_TAB;
}

#endif

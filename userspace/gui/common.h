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

static inline int gui_min_int(int left, int right) {
    return left < right ? left : right;
}

static inline ku_window_t gui_open(
    const char* title, int x, int y, int width, int height) {
    ku_ui_window_options options = {
        sizeof(ku_ui_window_options), x, y, width, height
    };
    ku_result_t result = ku_ui_create(title, strlen(title), &options);
    if (result > 0) return (ku_window_t)result;

    /*
     * Application layouts express preferred bounds. The desktop work area is
     * smaller than the physical mode because the top rail and task ribbon are
     * reserved by WindowManager. Older apps used physical-screen coordinates,
     * which made a perfectly valid app fail to start on otherwise supported
     * modes (notably Kurogane Web at 1280x800).
     *
     * Keep the public ABI unchanged and retry with two conservative layouts.
     * The first preserves a large working canvas on normal desktop modes; the
     * second is deliberately small enough for the 800x600 qualification mode.
     */
    options.x = 60;
    options.y = 60;
    options.width = gui_min_int(width, 900);
    options.height = gui_min_int(height, 600);
    result = ku_ui_create(title, strlen(title), &options);
    if (result > 0) return (ku_window_t)result;

    options.x = 40;
    options.y = 55;
    options.width = gui_min_int(width, 700);
    options.height = gui_min_int(height, 440);
    result = ku_ui_create(title, strlen(title), &options);
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

/*
 * Mirrors the stable ABI-v2 native_surface row metrics in kernel/ui/ui.cpp.
 * Pointer events are expected in content-local coordinates. Keeping this
 * arithmetic in one userspace helper means applications share one hit-test
 * contract instead of growing app-specific magic offsets.
 */
static inline int32_t gui_widget_height(uint32_t type) {
    switch (type) {
        case KU_UI_WIDGET_PANEL: return 38;
        case KU_UI_WIDGET_LABEL: return 26;
        case KU_UI_WIDGET_BUTTON:
        case KU_UI_WIDGET_INPUT:
        case KU_UI_WIDGET_LIST_ITEM: return 36;
        case KU_UI_WIDGET_PROGRESS: return 50;
        case KU_UI_WIDGET_SEPARATOR: return 12;
        default: return 26;
    }
}

static inline uint32_t gui_scene_hit_test_local(
    const kui_scene* scene,
    const ku_ui_event* event) {
    uint32_t visible_index = 0U;
    int32_t y = 10;
    uint32_t index;
    if (scene == NULL || event == NULL || event->type != KU_UI_EVENT_POINTER) return 0U;
    if ((event->buttons & 1U) == 0U || event->x < 8 || event->y < 0) return 0U;

    for (index = 0U; index < scene->view_count; ++index) {
        const kui_view* view = &scene->views[index];
        int32_t height;
        if ((view->flags & KUI_VIEW_HIDDEN) != 0U) continue;
        if (visible_index++ < scene->scroll_offset) continue;
        if (scene->visible_rows != 0U &&
            visible_index > scene->scroll_offset + scene->visible_rows) break;
        height = gui_widget_height(view->type);
        if (event->y >= y && event->y < y + height - 4) {
            if ((view->flags & KUI_VIEW_DISABLED) != 0U) return 0U;
            return view->id;
        }
        y += height;
    }
    return 0U;
}

#endif
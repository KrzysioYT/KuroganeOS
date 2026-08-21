#ifndef KUROGANE_SDK_UI_H
#define KUROGANE_SDK_UI_H

#include <kurogane/syscall.h>

#define KU_UI_ABI_VERSION_LEGACY UINT32_C(1)
#define KU_UI_ABI_VERSION UINT32_C(2)
#define KU_UI_MAX_LINES 12U
#define KU_UI_LINE_CAPACITY 64U
#define KU_UI_MAX_WIDGETS 32U
#define KU_UI_WIDGET_TEXT_CAPACITY KU_UI_LINE_CAPACITY

typedef uint32_t ku_window_t;
#define KU_INVALID_WINDOW UINT32_C(0)

/*
 * Public key values mirror the stable kernel KeyCode enumeration transported
 * through ku_ui_event.key. Applications must use these names instead of PS/2
 * scancodes or host-specific magic numbers.
 */
enum ku_ui_key_code {
    KU_UI_KEY_UNKNOWN = 0,
    KU_UI_KEY_ESCAPE = 1,
    KU_UI_KEY_BACKSPACE = 14,
    KU_UI_KEY_TAB = 15,
    KU_UI_KEY_ENTER = 28,
    KU_UI_KEY_HOME = 77,
    KU_UI_KEY_ARROW_UP = 78,
    KU_UI_KEY_PAGE_UP = 79,
    KU_UI_KEY_ARROW_LEFT = 80,
    KU_UI_KEY_ARROW_RIGHT = 81,
    KU_UI_KEY_END = 82,
    KU_UI_KEY_ARROW_DOWN = 83,
    KU_UI_KEY_PAGE_DOWN = 84,
    KU_UI_KEY_INSERT = 85,
    KU_UI_KEY_DELETE = 86
};

typedef struct ku_ui_window_options {
    uint32_t structure_size;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} ku_ui_window_options;

typedef struct ku_ui_frame {
    uint32_t structure_size;
    uint32_t background_rgb;
    uint32_t foreground_rgb;
    uint32_t accent_rgb;
    uint32_t line_count;
    uint32_t progress_value;
    uint32_t progress_maximum;
    uint32_t reserved;
    char lines[KU_UI_MAX_LINES][KU_UI_LINE_CAPACITY];
} ku_ui_frame;

enum ku_ui_widget_type {
    KU_UI_WIDGET_PANEL = 1,
    KU_UI_WIDGET_LABEL = 2,
    KU_UI_WIDGET_BUTTON = 3,
    KU_UI_WIDGET_INPUT = 4,
    KU_UI_WIDGET_LIST_ITEM = 5,
    KU_UI_WIDGET_PROGRESS = 6,
    KU_UI_WIDGET_SEPARATOR = 7
};

enum ku_ui_widget_flags {
    KU_UI_WIDGET_HIDDEN = UINT32_C(1) << 0,
    KU_UI_WIDGET_SELECTED = UINT32_C(1) << 1,
    KU_UI_WIDGET_DISABLED = UINT32_C(1) << 2
};

enum ku_ui_cursor {
    KU_UI_CURSOR_AUTO = 0,
    KU_UI_CURSOR_DEFAULT = 1,
    KU_UI_CURSOR_POINTER = 2,
    KU_UI_CURSOR_HAND = 3,
    KU_UI_CURSOR_TEXT = 4,
    KU_UI_CURSOR_WORKING = 5,
    KU_UI_CURSOR_BUSY = 6,
    KU_UI_CURSOR_MOVE = 7,
    KU_UI_CURSOR_RESIZE = 8,
    KU_UI_CURSOR_HELP = 9,
    KU_UI_CURSOR_NOT_ALLOWED = 10
};

/*
 * Native 5.0 widget transport. The legacy text frame remains accepted by the
 * kernel so older userspace binaries keep working, while new libui scenes
 * preserve widget type, hierarchy, state and icon identity end to end.
 */
typedef struct ku_ui_widget {
    uint32_t id;
    uint32_t parent_id;
    uint32_t type;
    uint32_t flags;
    uint32_t value;
    uint32_t maximum;
    uint32_t icon_id;
    uint32_t reserved;
    char text[KU_UI_WIDGET_TEXT_CAPACITY];
} ku_ui_widget;

typedef struct ku_ui_surface {
    uint32_t structure_size;
    uint32_t abi_version;
    uint32_t background_rgb;
    uint32_t foreground_rgb;
    uint32_t accent_rgb;
    uint32_t widget_count;
    uint32_t scroll_offset;
    uint32_t visible_rows;
    uint32_t selected_id;
    uint32_t cursor;
    uint32_t reserved[2];
    ku_ui_widget widgets[KU_UI_MAX_WIDGETS];
} ku_ui_surface;

enum ku_ui_event_type {
    KU_UI_EVENT_NONE = 0,
    KU_UI_EVENT_CLOSE = 1,
    KU_UI_EVENT_KEY = 2,
    KU_UI_EVENT_POINTER = 3
};

typedef struct ku_ui_event {
    uint32_t structure_size;
    uint32_t type;
    uint32_t key;
    uint32_t character;
    int32_t x;
    int32_t y;
    uint32_t buttons;
    uint32_t reserved;
} ku_ui_event;

static inline ku_result_t ku_ui_create(
    const char* title,
    size_t title_size,
    const ku_ui_window_options* options) {
    return ku_syscall3(
        KU_SYS_UI_CREATE, (uint64_t)(uintptr_t)title,
        (uint64_t)title_size, (uint64_t)(uintptr_t)options);
}

static inline ku_status_t ku_ui_present(
    ku_window_t window,
    const ku_ui_frame* frame) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_UI_PRESENT, window, (uint64_t)(uintptr_t)frame,
        sizeof(ku_ui_frame));
}

static inline ku_status_t ku_ui_present_surface(
    ku_window_t window,
    const ku_ui_surface* surface) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_UI_PRESENT, window, (uint64_t)(uintptr_t)surface,
        sizeof(ku_ui_surface));
}

static inline ku_status_t ku_ui_poll(
    ku_window_t window,
    ku_ui_event* event) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_UI_POLL, window, (uint64_t)(uintptr_t)event,
        sizeof(ku_ui_event));
}

static inline ku_status_t ku_ui_close(ku_window_t window) {
    return (ku_status_t)ku_syscall3(KU_SYS_UI_CLOSE, window, 0, 0);
}

#if defined(__cplusplus)
static_assert(sizeof(ku_ui_window_options) == 20, "UI window ABI mismatch");
static_assert(sizeof(ku_ui_event) == 32, "UI event ABI mismatch");
static_assert(sizeof(ku_ui_frame) == 800, "UI frame ABI mismatch");
static_assert(sizeof(ku_ui_widget) == 96, "UI widget ABI mismatch");
static_assert(sizeof(ku_ui_surface) == 3120, "UI surface ABI mismatch");
#else
_Static_assert(sizeof(ku_ui_window_options) == 20, "UI window ABI mismatch");
_Static_assert(sizeof(ku_ui_event) == 32, "UI event ABI mismatch");
_Static_assert(sizeof(ku_ui_frame) == 800, "UI frame ABI mismatch");
_Static_assert(sizeof(ku_ui_widget) == 96, "UI widget ABI mismatch");
_Static_assert(sizeof(ku_ui_surface) == 3120, "UI surface ABI mismatch");
#endif

#endif

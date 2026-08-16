#ifndef KUROGANE_SDK_UI_H
#define KUROGANE_SDK_UI_H

#include <kurogane/syscall.h>

#define KU_UI_ABI_VERSION UINT32_C(1)
#define KU_UI_MAX_LINES 12U
#define KU_UI_LINE_CAPACITY 64U

typedef uint32_t ku_window_t;
#define KU_INVALID_WINDOW UINT32_C(0)

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
#else
_Static_assert(sizeof(ku_ui_window_options) == 20, "UI window ABI mismatch");
_Static_assert(sizeof(ku_ui_event) == 32, "UI event ABI mismatch");
_Static_assert(sizeof(ku_ui_frame) == 800, "UI frame ABI mismatch");
#endif

#endif

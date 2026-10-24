#ifndef KUROGANE_SDK_DISPLAY_H
#define KUROGANE_SDK_DISPLAY_H

#include <stdint.h>
#include <kurogane/ui.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KU_DISPLAY_INFO_VERSION UINT32_C(1)

typedef struct ku_display_info {
    uint32_t structure_size;
    uint32_t version;
    uint32_t screen_width;
    uint32_t screen_height;
    int32_t work_x;
    int32_t work_y;
    int32_t work_width;
    int32_t work_height;
} ku_display_info;

#if defined(__cplusplus)
static_assert(sizeof(ku_display_info) == 32, "display ABI drift");
#else
_Static_assert(sizeof(ku_display_info) == 32, "display ABI drift");
#endif

static inline ku_status_t ku_display_get_info(ku_display_info* info) {
    if (info == NULL) return KU_STATUS_INVALID_ARGUMENT;
    return (ku_status_t)ku_syscall3(
        KU_SYS_DISPLAY_INFO,
        (uint64_t)(uintptr_t)info,
        (uint64_t)sizeof(*info), 0U);
}

/*
 * Geometry only: no syscall. Retain the application's requested dimensions
 * on ordinary desktop sizes while keeping new windows visible on narrow GOP
 * screens. Never allow caller-supplied or corrupt coordinates to overflow.
 * Does not replace kernel-side geometry validation.
 */
static inline int ku_display_fit_window(
    const ku_display_info* display, ku_ui_window_options* window) {
    int64_t left, top, right, bottom;
    int64_t width, height, x, y;
    if (display == NULL || window == NULL ||
        display->structure_size != sizeof(*display) ||
        display->version != KU_DISPLAY_INFO_VERSION ||
        window->structure_size != sizeof(*window) ||
        display->screen_width < 320U || display->screen_height < 200U ||
        display->work_width < 260 || display->work_height < 160 ||
        window->width <= 0 || window->height <= 0) return 0;
    left = (int64_t)display->work_x;
    top = (int64_t)display->work_y;
    right = left + (int64_t)display->work_width;
    bottom = top + (int64_t)display->work_height;
    if (left < 0 || top < 0 ||
        right > (int64_t)display->screen_width ||
        bottom > (int64_t)display->screen_height) return 0;

    width = window->width;
    height = window->height;
    if (width > display->work_width) width = display->work_width;
    if (height > display->work_height) height = display->work_height;
    if (width < 260) width = 260;
    if (height < 160) height = 160;
    x = window->x;
    y = window->y;
    if (x < left) x = left;
    if (y < top) y = top;
    if (x > right - width) x = right - width;
    if (y > bottom - height) y = bottom - height;
    window->x = (int32_t)x;
    window->y = (int32_t)y;
    window->width = (int32_t)width;
    window->height = (int32_t)height;
    return 1;
}

#ifdef __cplusplus
}
#endif
#endif

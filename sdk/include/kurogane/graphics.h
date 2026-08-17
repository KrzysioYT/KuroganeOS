#ifndef KUROGANE_SDK_GRAPHICS_H
#define KUROGANE_SDK_GRAPHICS_H

#include <stddef.h>
#include <stdint.h>
#include <kurogane/status.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KU_GFX_ABI_VERSION UINT32_C(1)
#define KU_GFX_FORMAT_XRGB8888 UINT32_C(1)
#define KU_GFX_MAX_DIMENSION UINT32_C(4096)

typedef uint32_t ku_gfx_color_t;

typedef struct ku_gfx_surface {
    uint32_t structure_size;
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t reserved;
    uint32_t* pixels;
    size_t pixel_capacity;
} ku_gfx_surface;

typedef struct ku_gfx_vertex2d {
    int32_t x;
    int32_t y;
    ku_gfx_color_t color;
} ku_gfx_vertex2d;

static inline ku_gfx_color_t ku_gfx_rgb(
    uint8_t red, uint8_t green, uint8_t blue) {
    return ((uint32_t)red << 16U) |
        ((uint32_t)green << 8U) |
        (uint32_t)blue;
}

static inline int ku_gfx_surface_valid(const ku_gfx_surface* surface) {
    size_t required;
    if (surface == NULL ||
        surface->structure_size != sizeof(ku_gfx_surface) ||
        surface->format != KU_GFX_FORMAT_XRGB8888 ||
        surface->pixels == NULL || surface->width == 0U || surface->height == 0U ||
        surface->width > KU_GFX_MAX_DIMENSION ||
        surface->height > KU_GFX_MAX_DIMENSION ||
        surface->stride_pixels < surface->width) {
        return 0;
    }
    if ((size_t)surface->height > SIZE_MAX / (size_t)surface->stride_pixels) {
        return 0;
    }
    required = (size_t)surface->height * (size_t)surface->stride_pixels;
    return required <= surface->pixel_capacity;
}

static inline ku_status_t ku_gfx_surface_init(
    ku_gfx_surface* surface,
    uint32_t* pixels,
    size_t pixel_capacity,
    uint32_t width,
    uint32_t height,
    uint32_t stride_pixels) {
    if (surface == NULL) return KU_STATUS_INVALID_ARGUMENT;
    surface->structure_size = sizeof(*surface);
    surface->format = KU_GFX_FORMAT_XRGB8888;
    surface->width = width;
    surface->height = height;
    surface->stride_pixels = stride_pixels;
    surface->reserved = 0U;
    surface->pixels = pixels;
    surface->pixel_capacity = pixel_capacity;
    return ku_gfx_surface_valid(surface)
        ? KU_STATUS_OK : KU_STATUS_INVALID_ARGUMENT;
}

static inline ku_status_t ku_gfx_clear(
    ku_gfx_surface* surface,
    ku_gfx_color_t color) {
    uint32_t y;
    if (!ku_gfx_surface_valid(surface)) return KU_STATUS_INVALID_ARGUMENT;
    color &= UINT32_C(0x00FFFFFF);
    for (y = 0U; y < surface->height; ++y) {
        uint32_t x;
        uint32_t* row = surface->pixels + (size_t)y * surface->stride_pixels;
        for (x = 0U; x < surface->width; ++x) row[x] = color;
    }
    return KU_STATUS_OK;
}

static inline ku_status_t ku_gfx_put_pixel(
    ku_gfx_surface* surface,
    int32_t x,
    int32_t y,
    ku_gfx_color_t color) {
    if (!ku_gfx_surface_valid(surface)) return KU_STATUS_INVALID_ARGUMENT;
    if (x < 0 || y < 0 || (uint32_t)x >= surface->width ||
        (uint32_t)y >= surface->height) {
        return KU_STATUS_OUT_OF_RANGE;
    }
    surface->pixels[(size_t)(uint32_t)y * surface->stride_pixels + (uint32_t)x] =
        color & UINT32_C(0x00FFFFFF);
    return KU_STATUS_OK;
}

static inline ku_status_t ku_gfx_fill_rect(
    ku_gfx_surface* surface,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    ku_gfx_color_t color) {
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
    int64_t yy;
    if (!ku_gfx_surface_valid(surface)) return KU_STATUS_INVALID_ARGUMENT;
    if (width <= 0 || height <= 0) return KU_STATUS_INVALID_ARGUMENT;
    left = x;
    top = y;
    right = left + (int64_t)width;
    bottom = top + (int64_t)height;
    if (right <= 0 || bottom <= 0 || left >= (int64_t)surface->width ||
        top >= (int64_t)surface->height) return KU_STATUS_OK;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > (int64_t)surface->width) right = surface->width;
    if (bottom > (int64_t)surface->height) bottom = surface->height;
    color &= UINT32_C(0x00FFFFFF);
    for (yy = top; yy < bottom; ++yy) {
        int64_t xx;
        uint32_t* row = surface->pixels + (size_t)yy * surface->stride_pixels;
        for (xx = left; xx < right; ++xx) row[(size_t)xx] = color;
    }
    return KU_STATUS_OK;
}

static inline int64_t ku_gfx_abs64(int64_t value) {
    return value < 0 ? -value : value;
}

static inline ku_status_t ku_gfx_draw_line(
    ku_gfx_surface* surface,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    ku_gfx_color_t color) {
    int64_t x = x0;
    int64_t y = y0;
    const int64_t target_x = x1;
    const int64_t target_y = y1;
    const int64_t dx = ku_gfx_abs64(target_x - x);
    const int64_t sx = x < target_x ? 1 : -1;
    const int64_t dy = -ku_gfx_abs64(target_y - y);
    const int64_t sy = y < target_y ? 1 : -1;
    int64_t error = dx + dy;
    uint64_t budget;
    if (!ku_gfx_surface_valid(surface)) return KU_STATUS_INVALID_ARGUMENT;
    budget = (uint64_t)ku_gfx_abs64(target_x - x) +
        (uint64_t)ku_gfx_abs64(target_y - y) + UINT64_C(2);
    if (budget > UINT64_C(16384)) return KU_STATUS_OUT_OF_RANGE;
    while (budget-- != 0U) {
        if (x >= 0 && y >= 0 && x < (int64_t)surface->width &&
            y < (int64_t)surface->height) {
            surface->pixels[(size_t)y * surface->stride_pixels + (size_t)x] =
                color & UINT32_C(0x00FFFFFF);
        }
        if (x == target_x && y == target_y) return KU_STATUS_OK;
        {
            const int64_t doubled = error * 2;
            if (doubled >= dy) { error += dy; x += sx; }
            if (doubled <= dx) { error += dx; y += sy; }
        }
    }
    return KU_STATUS_OUT_OF_RANGE;
}

static inline int64_t ku_gfx_edge(
    int32_t ax, int32_t ay,
    int32_t bx, int32_t by,
    int32_t px, int32_t py) {
    return ((int64_t)px - ax) * ((int64_t)by - ay) -
        ((int64_t)py - ay) * ((int64_t)bx - ax);
}

static inline ku_status_t ku_gfx_fill_triangle(
    ku_gfx_surface* surface,
    const ku_gfx_vertex2d* a,
    const ku_gfx_vertex2d* b,
    const ku_gfx_vertex2d* c) {
    int32_t min_x;
    int32_t max_x;
    int32_t min_y;
    int32_t max_y;
    int64_t area;
    int32_t y;
    ku_gfx_color_t color;
    if (!ku_gfx_surface_valid(surface) || a == NULL || b == NULL || c == NULL) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    area = ku_gfx_edge(a->x, a->y, b->x, b->y, c->x, c->y);
    if (area == 0) return KU_STATUS_INVALID_ARGUMENT;
    min_x = a->x < b->x ? a->x : b->x;
    if (c->x < min_x) min_x = c->x;
    max_x = a->x > b->x ? a->x : b->x;
    if (c->x > max_x) max_x = c->x;
    min_y = a->y < b->y ? a->y : b->y;
    if (c->y < min_y) min_y = c->y;
    max_y = a->y > b->y ? a->y : b->y;
    if (c->y > max_y) max_y = c->y;
    if (max_x < 0 || max_y < 0 || min_x >= (int32_t)surface->width ||
        min_y >= (int32_t)surface->height) return KU_STATUS_OK;
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= (int32_t)surface->width) max_x = (int32_t)surface->width - 1;
    if (max_y >= (int32_t)surface->height) max_y = (int32_t)surface->height - 1;
    color = a->color & UINT32_C(0x00FFFFFF);
    for (y = min_y; y <= max_y; ++y) {
        int32_t x;
        uint32_t* row = surface->pixels + (size_t)(uint32_t)y * surface->stride_pixels;
        for (x = min_x; x <= max_x; ++x) {
            const int64_t e0 = ku_gfx_edge(a->x, a->y, b->x, b->y, x, y);
            const int64_t e1 = ku_gfx_edge(b->x, b->y, c->x, c->y, x, y);
            const int64_t e2 = ku_gfx_edge(c->x, c->y, a->x, a->y, x, y);
            if ((area > 0 && e0 >= 0 && e1 >= 0 && e2 >= 0) ||
                (area < 0 && e0 <= 0 && e1 <= 0 && e2 <= 0)) {
                row[(uint32_t)x] = color;
            }
        }
    }
    return KU_STATUS_OK;
}

#ifdef __cplusplus
}
static_assert(sizeof(ku_gfx_vertex2d) == 12U, "graphics vertex ABI mismatch");
#else
_Static_assert(sizeof(ku_gfx_vertex2d) == 12U, "graphics vertex ABI mismatch");
#endif

#endif

#ifndef KUROGANE_SDK_GRAPHICS_H
#define KUROGANE_SDK_GRAPHICS_H

#include <stddef.h>
#include <stdint.h>
#include <kurogane/status.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KU_GFX_ABI_VERSION UINT32_C(2)
#define KU_GFX_FORMAT_XRGB8888 UINT32_C(1)
#define KU_GFX_MAX_DIMENSION UINT32_C(4096)
#define KU_GFX_FIXED_ONE INT32_C(65536)
#define KU_GFX_DEPTH_FAR UINT32_MAX
#define KU_GFX_MAX_TRIANGLE_WORK UINT64_C(4194304)

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

typedef struct ku_gfx_rect {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} ku_gfx_rect;

typedef struct ku_gfx_vertex2d {
    int32_t x;
    int32_t y;
    ku_gfx_color_t color;
} ku_gfx_vertex2d;

/*
 * Screen-space fixed-point 3D vertex used by the bounded software backend.
 * z uses 0=near, UINT32_MAX=far. u/v are texel coordinates in 16.16 fixed
 * point so the freestanding implementation needs neither x87 nor SSE.
 */
typedef struct ku_gfx_vertex3d {
    int32_t x;
    int32_t y;
    uint32_t z;
    int32_t u16_16;
    int32_t v16_16;
    ku_gfx_color_t color;
} ku_gfx_vertex3d;

typedef struct ku_gfx_depth_surface {
    uint32_t structure_size;
    uint32_t width;
    uint32_t height;
    uint32_t stride_values;
    uint32_t* values;
    size_t value_capacity;
} ku_gfx_depth_surface;

typedef struct ku_gfx_texture2d {
    uint32_t structure_size;
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t reserved;
    const uint32_t* pixels;
    size_t pixel_capacity;
} ku_gfx_texture2d;

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

static inline int ku_gfx_depth_surface_valid(const ku_gfx_depth_surface* surface) {
    size_t required;
    if (surface == NULL || surface->structure_size != sizeof(*surface) ||
        surface->values == NULL || surface->width == 0U || surface->height == 0U ||
        surface->width > KU_GFX_MAX_DIMENSION ||
        surface->height > KU_GFX_MAX_DIMENSION ||
        surface->stride_values < surface->width) {
        return 0;
    }
    if ((size_t)surface->height > SIZE_MAX / (size_t)surface->stride_values) return 0;
    required = (size_t)surface->height * (size_t)surface->stride_values;
    return required <= surface->value_capacity;
}

static inline int ku_gfx_texture2d_valid(const ku_gfx_texture2d* texture) {
    size_t required;
    if (texture == NULL || texture->structure_size != sizeof(*texture) ||
        texture->format != KU_GFX_FORMAT_XRGB8888 || texture->pixels == NULL ||
        texture->width == 0U || texture->height == 0U ||
        texture->width > KU_GFX_MAX_DIMENSION ||
        texture->height > KU_GFX_MAX_DIMENSION ||
        texture->stride_pixels < texture->width) return 0;
    if ((size_t)texture->height > SIZE_MAX / (size_t)texture->stride_pixels) return 0;
    required = (size_t)texture->height * (size_t)texture->stride_pixels;
    return required <= texture->pixel_capacity;
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

static inline ku_status_t ku_gfx_depth_surface_init(
    ku_gfx_depth_surface* surface,
    uint32_t* values,
    size_t value_capacity,
    uint32_t width,
    uint32_t height,
    uint32_t stride_values) {
    if (surface == NULL) return KU_STATUS_INVALID_ARGUMENT;
    surface->structure_size = sizeof(*surface);
    surface->width = width;
    surface->height = height;
    surface->stride_values = stride_values;
    surface->values = values;
    surface->value_capacity = value_capacity;
    return ku_gfx_depth_surface_valid(surface)
        ? KU_STATUS_OK : KU_STATUS_INVALID_ARGUMENT;
}

static inline ku_status_t ku_gfx_texture2d_init(
    ku_gfx_texture2d* texture,
    const uint32_t* pixels,
    size_t pixel_capacity,
    uint32_t width,
    uint32_t height,
    uint32_t stride_pixels) {
    if (texture == NULL) return KU_STATUS_INVALID_ARGUMENT;
    texture->structure_size = sizeof(*texture);
    texture->format = KU_GFX_FORMAT_XRGB8888;
    texture->width = width;
    texture->height = height;
    texture->stride_pixels = stride_pixels;
    texture->reserved = 0U;
    texture->pixels = pixels;
    texture->pixel_capacity = pixel_capacity;
    return ku_gfx_texture2d_valid(texture)
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

static inline ku_status_t ku_gfx_clear_depth(
    ku_gfx_depth_surface* surface,
    uint32_t depth) {
    uint32_t y;
    if (!ku_gfx_depth_surface_valid(surface)) return KU_STATUS_INVALID_ARGUMENT;
    for (y = 0U; y < surface->height; ++y) {
        uint32_t x;
        uint32_t* row = surface->values + (size_t)y * surface->stride_values;
        for (x = 0U; x < surface->width; ++x) row[x] = depth;
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

static inline int ku_gfx_clip_rect(
    const ku_gfx_surface* surface,
    const ku_gfx_rect* requested,
    ku_gfx_rect* output) {
    int64_t left = 0;
    int64_t top = 0;
    int64_t right;
    int64_t bottom;
    if (!ku_gfx_surface_valid(surface) || output == NULL) return 0;
    right = surface->width;
    bottom = surface->height;
    if (requested != NULL) {
        int64_t requested_right;
        int64_t requested_bottom;
        if (requested->width <= 0 || requested->height <= 0) return 0;
        requested_right = (int64_t)requested->x + requested->width;
        requested_bottom = (int64_t)requested->y + requested->height;
        if ((int64_t)requested->x > left) left = requested->x;
        if ((int64_t)requested->y > top) top = requested->y;
        if (requested_right < right) right = requested_right;
        if (requested_bottom < bottom) bottom = requested_bottom;
    }
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > (int64_t)surface->width) right = surface->width;
    if (bottom > (int64_t)surface->height) bottom = surface->height;
    if (right <= left || bottom <= top || left > INT32_MAX || top > INT32_MAX ||
        right - left > INT32_MAX || bottom - top > INT32_MAX) return 0;
    output->x = (int32_t)left;
    output->y = (int32_t)top;
    output->width = (int32_t)(right - left);
    output->height = (int32_t)(bottom - top);
    return 1;
}

static inline ku_status_t ku_gfx_triangle_work_estimate(
    const ku_gfx_surface* surface,
    int32_t ax, int32_t ay,
    int32_t bx, int32_t by,
    int32_t cx, int32_t cy,
    const ku_gfx_rect* clip,
    uint64_t* out_work,
    ku_gfx_rect* out_bounds) {
    int32_t min_x;
    int32_t max_x;
    int32_t min_y;
    int32_t max_y;
    ku_gfx_rect clipped;
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
    uint64_t width;
    uint64_t height;
    if (out_work != NULL) *out_work = 0U;
    if (!ku_gfx_surface_valid(surface) || out_work == NULL) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    if (!ku_gfx_clip_rect(surface, clip, &clipped)) return KU_STATUS_OK;
    min_x = ax < bx ? ax : bx;
    if (cx < min_x) min_x = cx;
    max_x = ax > bx ? ax : bx;
    if (cx > max_x) max_x = cx;
    min_y = ay < by ? ay : by;
    if (cy < min_y) min_y = cy;
    max_y = ay > by ? ay : by;
    if (cy > max_y) max_y = cy;
    left = min_x;
    top = min_y;
    right = (int64_t)max_x + 1;
    bottom = (int64_t)max_y + 1;
    if (left < clipped.x) left = clipped.x;
    if (top < clipped.y) top = clipped.y;
    if (right > (int64_t)clipped.x + clipped.width) right = (int64_t)clipped.x + clipped.width;
    if (bottom > (int64_t)clipped.y + clipped.height) bottom = (int64_t)clipped.y + clipped.height;
    if (right <= left || bottom <= top) return KU_STATUS_OK;
    width = (uint64_t)(right - left);
    height = (uint64_t)(bottom - top);
    if (height != 0U && width > UINT64_MAX / height) return KU_STATUS_OUT_OF_RANGE;
    *out_work = width * height;
    if (out_bounds != NULL) {
        out_bounds->x = (int32_t)left;
        out_bounds->y = (int32_t)top;
        out_bounds->width = (int32_t)width;
        out_bounds->height = (int32_t)height;
    }
    return KU_STATUS_OK;
}

static inline ku_status_t ku_gfx_fill_triangle_clipped(
    ku_gfx_surface* surface,
    const ku_gfx_vertex2d* a,
    const ku_gfx_vertex2d* b,
    const ku_gfx_vertex2d* c,
    const ku_gfx_rect* clip,
    uint64_t work_budget,
    uint64_t* out_work) {
    ku_gfx_rect bounds;
    uint64_t work = 0U;
    int64_t area;
    int32_t y;
    ku_gfx_color_t color;
    ku_status_t estimate;
    if (out_work != NULL) *out_work = 0U;
    if (!ku_gfx_surface_valid(surface) || a == NULL || b == NULL || c == NULL ||
        work_budget == 0U) return KU_STATUS_INVALID_ARGUMENT;
    area = ku_gfx_edge(a->x, a->y, b->x, b->y, c->x, c->y);
    if (area == 0) return KU_STATUS_INVALID_ARGUMENT;
    estimate = ku_gfx_triangle_work_estimate(
        surface, a->x, a->y, b->x, b->y, c->x, c->y, clip, &work, &bounds);
    if (estimate != KU_STATUS_OK) return estimate;
    if (work == 0U) return KU_STATUS_OK;
    if (work > work_budget || work > KU_GFX_MAX_TRIANGLE_WORK) return KU_STATUS_WOULD_BLOCK;
    color = a->color & UINT32_C(0x00FFFFFF);
    for (y = bounds.y; y < bounds.y + bounds.height; ++y) {
        int32_t x;
        uint32_t* row = surface->pixels + (size_t)(uint32_t)y * surface->stride_pixels;
        for (x = bounds.x; x < bounds.x + bounds.width; ++x) {
            const int64_t e0 = ku_gfx_edge(a->x, a->y, b->x, b->y, x, y);
            const int64_t e1 = ku_gfx_edge(b->x, b->y, c->x, c->y, x, y);
            const int64_t e2 = ku_gfx_edge(c->x, c->y, a->x, a->y, x, y);
            if ((area > 0 && e0 >= 0 && e1 >= 0 && e2 >= 0) ||
                (area < 0 && e0 <= 0 && e1 <= 0 && e2 <= 0)) {
                row[(uint32_t)x] = color;
            }
        }
    }
    if (out_work != NULL) *out_work = work;
    return KU_STATUS_OK;
}

static inline ku_status_t ku_gfx_fill_triangle(
    ku_gfx_surface* surface,
    const ku_gfx_vertex2d* a,
    const ku_gfx_vertex2d* b,
    const ku_gfx_vertex2d* c) {
    return ku_gfx_fill_triangle_clipped(
        surface, a, b, c, NULL, KU_GFX_MAX_TRIANGLE_WORK, NULL);
}

static inline ku_gfx_color_t ku_gfx_modulate_color(
    ku_gfx_color_t left,
    ku_gfx_color_t right) {
    const uint32_t lr = (left >> 16U) & UINT32_C(0xFF);
    const uint32_t lg = (left >> 8U) & UINT32_C(0xFF);
    const uint32_t lb = left & UINT32_C(0xFF);
    const uint32_t rr = (right >> 16U) & UINT32_C(0xFF);
    const uint32_t rg = (right >> 8U) & UINT32_C(0xFF);
    const uint32_t rb = right & UINT32_C(0xFF);
    return ku_gfx_rgb(
        (uint8_t)((lr * rr + 127U) / 255U),
        (uint8_t)((lg * rg + 127U) / 255U),
        (uint8_t)((lb * rb + 127U) / 255U));
}

static inline ku_gfx_color_t ku_gfx_texture_sample_nearest(
    const ku_gfx_texture2d* texture,
    int32_t u16_16,
    int32_t v16_16) {
    uint32_t x;
    uint32_t y;
    if (!ku_gfx_texture2d_valid(texture)) return UINT32_C(0x00FFFFFF);
    if (u16_16 <= 0) x = 0U;
    else {
        uint32_t candidate = (uint32_t)u16_16 >> 16U;
        x = candidate < texture->width ? candidate : texture->width - 1U;
    }
    if (v16_16 <= 0) y = 0U;
    else {
        uint32_t candidate = (uint32_t)v16_16 >> 16U;
        y = candidate < texture->height ? candidate : texture->height - 1U;
    }
    return texture->pixels[(size_t)y * texture->stride_pixels + x] & UINT32_C(0x00FFFFFF);
}

static inline uint32_t ku_gfx_interpolate_channel(
    uint64_t w0,
    uint64_t w1,
    uint64_t w2,
    uint64_t area,
    uint32_t c0,
    uint32_t c1,
    uint32_t c2) {
    return area == 0U ? 0U : (uint32_t)(
        (w0 * c0 + w1 * c1 + w2 * c2) / area);
}

static inline ku_status_t ku_gfx_raster_triangle3d(
    ku_gfx_surface* surface,
    ku_gfx_depth_surface* depth,
    const ku_gfx_texture2d* texture,
    const ku_gfx_rect* clip,
    const ku_gfx_vertex3d* a,
    const ku_gfx_vertex3d* b,
    const ku_gfx_vertex3d* c,
    uint64_t work_budget,
    uint64_t* out_work) {
    ku_gfx_rect bounds;
    uint64_t work = 0U;
    int64_t signed_area;
    uint64_t area;
    int32_t y;
    ku_status_t estimate;
    if (out_work != NULL) *out_work = 0U;
    if (!ku_gfx_surface_valid(surface) || a == NULL || b == NULL || c == NULL ||
        work_budget == 0U) return KU_STATUS_INVALID_ARGUMENT;
    if (depth != NULL && (!ku_gfx_depth_surface_valid(depth) ||
        depth->width != surface->width || depth->height != surface->height)) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    if (texture != NULL && !ku_gfx_texture2d_valid(texture)) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    signed_area = ku_gfx_edge(a->x, a->y, b->x, b->y, c->x, c->y);
    if (signed_area == 0) return KU_STATUS_INVALID_ARGUMENT;
    area = (uint64_t)ku_gfx_abs64(signed_area);
    estimate = ku_gfx_triangle_work_estimate(
        surface, a->x, a->y, b->x, b->y, c->x, c->y, clip, &work, &bounds);
    if (estimate != KU_STATUS_OK) return estimate;
    if (work == 0U) return KU_STATUS_OK;
    if (work > work_budget || work > KU_GFX_MAX_TRIANGLE_WORK) return KU_STATUS_WOULD_BLOCK;

    for (y = bounds.y; y < bounds.y + bounds.height; ++y) {
        int32_t x;
        uint32_t* color_row = surface->pixels + (size_t)(uint32_t)y * surface->stride_pixels;
        uint32_t* depth_row = depth != NULL
            ? depth->values + (size_t)(uint32_t)y * depth->stride_values
            : NULL;
        for (x = bounds.x; x < bounds.x + bounds.width; ++x) {
            int64_t sw0 = ku_gfx_edge(b->x, b->y, c->x, c->y, x, y);
            int64_t sw1 = ku_gfx_edge(c->x, c->y, a->x, a->y, x, y);
            int64_t sw2 = ku_gfx_edge(a->x, a->y, b->x, b->y, x, y);
            uint64_t w0;
            uint64_t w1;
            uint64_t w2;
            uint32_t z;
            uint32_t red;
            uint32_t green;
            uint32_t blue;
            ku_gfx_color_t color;
            if ((signed_area > 0 && (sw0 < 0 || sw1 < 0 || sw2 < 0)) ||
                (signed_area < 0 && (sw0 > 0 || sw1 > 0 || sw2 > 0))) continue;
            if (signed_area < 0) { sw0 = -sw0; sw1 = -sw1; sw2 = -sw2; }
            w0 = (uint64_t)sw0;
            w1 = (uint64_t)sw1;
            w2 = (uint64_t)sw2;
            z = (uint32_t)((w0 * a->z + w1 * b->z + w2 * c->z) / area);
            if (depth_row != NULL && z >= depth_row[(uint32_t)x]) continue;
            red = ku_gfx_interpolate_channel(
                w0, w1, w2, area,
                (a->color >> 16U) & 0xFFU,
                (b->color >> 16U) & 0xFFU,
                (c->color >> 16U) & 0xFFU);
            green = ku_gfx_interpolate_channel(
                w0, w1, w2, area,
                (a->color >> 8U) & 0xFFU,
                (b->color >> 8U) & 0xFFU,
                (c->color >> 8U) & 0xFFU);
            blue = ku_gfx_interpolate_channel(
                w0, w1, w2, area,
                a->color & 0xFFU, b->color & 0xFFU, c->color & 0xFFU);
            color = ku_gfx_rgb((uint8_t)red, (uint8_t)green, (uint8_t)blue);
            if (texture != NULL) {
                const int64_t u_numerator =
                    (int64_t)w0 * a->u16_16 +
                    (int64_t)w1 * b->u16_16 +
                    (int64_t)w2 * c->u16_16;
                const int64_t v_numerator =
                    (int64_t)w0 * a->v16_16 +
                    (int64_t)w1 * b->v16_16 +
                    (int64_t)w2 * c->v16_16;
                const int32_t u = (int32_t)(u_numerator / (int64_t)area);
                const int32_t v = (int32_t)(v_numerator / (int64_t)area);
                color = ku_gfx_modulate_color(
                    color,
                    ku_gfx_texture_sample_nearest(texture, u, v));
            }
            color_row[(uint32_t)x] = color;
            if (depth_row != NULL) depth_row[(uint32_t)x] = z;
        }
    }
    if (out_work != NULL) *out_work = work;
    return KU_STATUS_OK;
}

#ifdef __cplusplus
}
static_assert(sizeof(ku_gfx_vertex2d) == 12U, "graphics vertex ABI mismatch");
static_assert(sizeof(ku_gfx_vertex3d) == 24U, "graphics 3D vertex ABI mismatch");
#else
_Static_assert(sizeof(ku_gfx_vertex2d) == 12U, "graphics vertex ABI mismatch");
_Static_assert(sizeof(ku_gfx_vertex3d) == 24U, "graphics 3D vertex ABI mismatch");
#endif

#endif

#ifndef KUROGANE_SDK_DIRECT3D_H
#define KUROGANE_SDK_DIRECT3D_H

#include <stddef.h>
#include <stdint.h>
#include <kurogane/graphics.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Kurogane Direct3D compatibility runtime.
 *
 * This remains a source-level Kurogane API rather than Microsoft's Windows
 * COM ABI. The software backend now provides bounded viewport/scissor state,
 * depth testing, texture sampling and retained D3D12-style command lists.
 * Unsupported Windows semantics still fail explicitly instead of pretending
 * to be implemented.
 */
#define KU_D3D_COMPAT_ABI_VERSION UINT32_C(2)
#define KU_D3D12_MAX_COMMANDS UINT32_C(64)
#define KU_D3D_MAX_DRAWS_PER_FRAME UINT32_C(4096)
#define KU_D3D_MAX_RASTER_PIXELS_PER_FRAME UINT64_C(8388608)

enum ku_d3d_frontend {
    KU_D3D_FRONTEND_9 = 9,
    KU_D3D_FRONTEND_11 = 11,
    KU_D3D_FRONTEND_12 = 12
};

enum ku_d3d_backend {
    KU_D3D_BACKEND_SOFTWARE = 1,
    KU_D3D_BACKEND_HARDWARE = 2
};

typedef struct ku_d3d_device {
    uint32_t structure_size;
    uint32_t frontend;
    uint32_t backend;
    uint32_t scene_active;
    ku_gfx_surface* target;
    uint64_t frame_index;
    uint32_t draw_calls;
    uint32_t draw_budget;
    ku_gfx_depth_surface* depth_target;
    const ku_gfx_texture2d* texture0;
    ku_gfx_rect viewport;
    ku_gfx_rect scissor;
    uint32_t scissor_enabled;
    uint32_t reserved;
    uint64_t raster_pixels;
    uint64_t raster_budget;
} ku_d3d_device;

enum ku_d3d12_command_type {
    KU_D3D12_COMMAND_CLEAR = 1,
    KU_D3D12_COMMAND_TRIANGLE = 2,
    KU_D3D12_COMMAND_CLEAR_DEPTH = 3,
    KU_D3D12_COMMAND_TRIANGLE3D = 4
};

typedef struct ku_d3d12_command {
    uint32_t type;
    uint32_t reserved;
    ku_gfx_color_t color;
    uint32_t depth;
    ku_gfx_vertex2d vertices[3];
    ku_gfx_vertex3d vertices3d[3];
} ku_d3d12_command;

typedef struct ku_d3d12_command_list {
    uint32_t structure_size;
    uint32_t count;
    ku_d3d12_command commands[KU_D3D12_MAX_COMMANDS];
} ku_d3d12_command_list;

static inline int ku_d3d_frontend_valid(uint32_t frontend) {
    return frontend == KU_D3D_FRONTEND_9 ||
        frontend == KU_D3D_FRONTEND_11 ||
        frontend == KU_D3D_FRONTEND_12;
}

static inline int ku_d3d_rect_inside_target(
    const ku_gfx_surface* target,
    const ku_gfx_rect* rect) {
    int64_t right;
    int64_t bottom;
    if (!ku_gfx_surface_valid(target) || rect == NULL ||
        rect->x < 0 || rect->y < 0 || rect->width <= 0 || rect->height <= 0) {
        return 0;
    }
    right = (int64_t)rect->x + rect->width;
    bottom = (int64_t)rect->y + rect->height;
    return right <= target->width && bottom <= target->height;
}

static inline ku_status_t ku_d3d_create_software_device(
    ku_d3d_device* device,
    uint32_t frontend,
    ku_gfx_surface* target) {
    if (device == NULL || !ku_d3d_frontend_valid(frontend) ||
        !ku_gfx_surface_valid(target)) return KU_STATUS_INVALID_ARGUMENT;
    device->structure_size = sizeof(*device);
    device->frontend = frontend;
    device->backend = KU_D3D_BACKEND_SOFTWARE;
    device->scene_active = 0U;
    device->target = target;
    device->frame_index = 0U;
    device->draw_calls = 0U;
    device->draw_budget = KU_D3D_MAX_DRAWS_PER_FRAME;
    device->depth_target = NULL;
    device->texture0 = NULL;
    device->viewport.x = 0;
    device->viewport.y = 0;
    device->viewport.width = (int32_t)target->width;
    device->viewport.height = (int32_t)target->height;
    device->scissor = device->viewport;
    device->scissor_enabled = 0U;
    device->reserved = 0U;
    device->raster_pixels = 0U;
    device->raster_budget = KU_D3D_MAX_RASTER_PIXELS_PER_FRAME;
    return KU_STATUS_OK;
}

static inline int ku_d3d_device_valid(const ku_d3d_device* device) {
    return device != NULL && device->structure_size == sizeof(*device) &&
        ku_d3d_frontend_valid(device->frontend) &&
        device->backend == KU_D3D_BACKEND_SOFTWARE &&
        ku_gfx_surface_valid(device->target) &&
        device->draw_budget != 0U &&
        device->draw_budget <= KU_D3D_MAX_DRAWS_PER_FRAME &&
        device->raster_budget != 0U &&
        device->raster_budget <= KU_D3D_MAX_RASTER_PIXELS_PER_FRAME &&
        device->raster_pixels <= device->raster_budget &&
        ku_d3d_rect_inside_target(device->target, &device->viewport) &&
        (device->scissor_enabled == 0U ||
         ku_d3d_rect_inside_target(device->target, &device->scissor)) &&
        (device->depth_target == NULL ||
         (ku_gfx_depth_surface_valid(device->depth_target) &&
          device->depth_target->width == device->target->width &&
          device->depth_target->height == device->target->height)) &&
        (device->texture0 == NULL || ku_gfx_texture2d_valid(device->texture0));
}

static inline ku_status_t ku_d3d_set_frame_budgets(
    ku_d3d_device* device,
    uint32_t draw_budget,
    uint64_t raster_budget) {
    if (!ku_d3d_device_valid(device)) return KU_STATUS_INVALID_ARGUMENT;
    if (draw_budget == 0U || draw_budget > KU_D3D_MAX_DRAWS_PER_FRAME ||
        raster_budget == 0U || raster_budget > KU_D3D_MAX_RASTER_PIXELS_PER_FRAME ||
        device->draw_calls > draw_budget || device->raster_pixels > raster_budget) {
        return KU_STATUS_OUT_OF_RANGE;
    }
    device->draw_budget = draw_budget;
    device->raster_budget = raster_budget;
    return KU_STATUS_OK;
}

static inline ku_status_t ku_d3d_set_viewport(
    ku_d3d_device* device,
    const ku_gfx_rect* viewport) {
    if (!ku_d3d_device_valid(device) ||
        !ku_d3d_rect_inside_target(device->target, viewport)) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    device->viewport = *viewport;
    return KU_STATUS_OK;
}

static inline ku_status_t ku_d3d_set_scissor(
    ku_d3d_device* device,
    const ku_gfx_rect* scissor) {
    if (!ku_d3d_device_valid(device)) return KU_STATUS_INVALID_ARGUMENT;
    if (scissor == NULL) {
        device->scissor_enabled = 0U;
        return KU_STATUS_OK;
    }
    if (!ku_d3d_rect_inside_target(device->target, scissor)) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    device->scissor = *scissor;
    device->scissor_enabled = 1U;
    return KU_STATUS_OK;
}

static inline ku_status_t ku_d3d_set_depth_surface(
    ku_d3d_device* device,
    ku_gfx_depth_surface* depth) {
    if (!ku_d3d_device_valid(device)) return KU_STATUS_INVALID_ARGUMENT;
    if (depth != NULL && (!ku_gfx_depth_surface_valid(depth) ||
        depth->width != device->target->width ||
        depth->height != device->target->height)) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    device->depth_target = depth;
    return KU_STATUS_OK;
}

static inline ku_status_t ku_d3d_set_texture0(
    ku_d3d_device* device,
    const ku_gfx_texture2d* texture) {
    if (!ku_d3d_device_valid(device)) return KU_STATUS_INVALID_ARGUMENT;
    if (texture != NULL && !ku_gfx_texture2d_valid(texture)) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    device->texture0 = texture;
    return KU_STATUS_OK;
}

static inline ku_gfx_rect ku_d3d_effective_clip(const ku_d3d_device* device) {
    ku_gfx_rect result = device->viewport;
    if (device->scissor_enabled != 0U) {
        const int32_t left = result.x > device->scissor.x
            ? result.x : device->scissor.x;
        const int32_t top = result.y > device->scissor.y
            ? result.y : device->scissor.y;
        const int64_t viewport_right = (int64_t)result.x + result.width;
        const int64_t viewport_bottom = (int64_t)result.y + result.height;
        const int64_t scissor_right = (int64_t)device->scissor.x + device->scissor.width;
        const int64_t scissor_bottom = (int64_t)device->scissor.y + device->scissor.height;
        const int64_t right = viewport_right < scissor_right ? viewport_right : scissor_right;
        const int64_t bottom = viewport_bottom < scissor_bottom ? viewport_bottom : scissor_bottom;
        result.x = left;
        result.y = top;
        result.width = right > left ? (int32_t)(right - left) : 0;
        result.height = bottom > top ? (int32_t)(bottom - top) : 0;
    }
    return result;
}

static inline ku_status_t ku_d3d_charge_raster(
    ku_d3d_device* device,
    uint64_t pixels) {
    if (!ku_d3d_device_valid(device)) return KU_STATUS_INVALID_ARGUMENT;
    if (pixels > device->raster_budget - device->raster_pixels) {
        return KU_STATUS_WOULD_BLOCK;
    }
    device->raster_pixels += pixels;
    return KU_STATUS_OK;
}

static inline ku_status_t ku_d3d_begin_scene(ku_d3d_device* device) {
    if (!ku_d3d_device_valid(device)) return KU_STATUS_INVALID_ARGUMENT;
    if (device->scene_active != 0U) return KU_STATUS_BAD_STATE;
    device->scene_active = 1U;
    return KU_STATUS_OK;
}

static inline ku_status_t ku_d3d_end_scene(ku_d3d_device* device) {
    if (!ku_d3d_device_valid(device)) return KU_STATUS_INVALID_ARGUMENT;
    if (device->scene_active == 0U) return KU_STATUS_BAD_STATE;
    device->scene_active = 0U;
    return KU_STATUS_OK;
}

static inline ku_status_t ku_d3d_clear(
    ku_d3d_device* device,
    ku_gfx_color_t color) {
    uint64_t pixels;
    ku_status_t status;
    if (!ku_d3d_device_valid(device)) return KU_STATUS_INVALID_ARGUMENT;
    pixels = (uint64_t)device->target->width * device->target->height;
    status = ku_d3d_charge_raster(device, pixels);
    if (status != KU_STATUS_OK) return status;
    status = ku_gfx_clear(device->target, color);
    if (status != KU_STATUS_OK) device->raster_pixels -= pixels;
    return status;
}

static inline ku_status_t ku_d3d_clear_depth(
    ku_d3d_device* device,
    uint32_t depth) {
    uint64_t pixels;
    ku_status_t status;
    if (!ku_d3d_device_valid(device) || device->depth_target == NULL) {
        return KU_STATUS_BAD_STATE;
    }
    pixels = (uint64_t)device->depth_target->width * device->depth_target->height;
    status = ku_d3d_charge_raster(device, pixels);
    if (status != KU_STATUS_OK) return status;
    status = ku_gfx_clear_depth(device->depth_target, depth);
    if (status != KU_STATUS_OK) device->raster_pixels -= pixels;
    return status;
}

static inline ku_status_t ku_d3d_draw_triangle(
    ku_d3d_device* device,
    const ku_gfx_vertex2d* a,
    const ku_gfx_vertex2d* b,
    const ku_gfx_vertex2d* c) {
    ku_status_t status;
    uint64_t work = 0U;
    ku_gfx_rect clip;
    if (!ku_d3d_device_valid(device) || a == NULL || b == NULL || c == NULL) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    if (device->frontend == KU_D3D_FRONTEND_9 && device->scene_active == 0U) {
        return KU_STATUS_BAD_STATE;
    }
    clip = ku_d3d_effective_clip(device);
    if (clip.width <= 0 || clip.height <= 0) return KU_STATUS_OK;
    if (device->draw_calls >= device->draw_budget) return KU_STATUS_WOULD_BLOCK;
    status = ku_gfx_fill_triangle_clipped(
        device->target, a, b, c, &clip,
        device->raster_budget - device->raster_pixels, &work);
    if (status == KU_STATUS_OK) {
        ++device->draw_calls;
        device->raster_pixels += work;
    }
    return status;
}

static inline ku_status_t ku_d3d_draw_triangle3d(
    ku_d3d_device* device,
    const ku_gfx_vertex3d* a,
    const ku_gfx_vertex3d* b,
    const ku_gfx_vertex3d* c) {
    ku_status_t status;
    uint64_t work = 0U;
    ku_gfx_rect clip;
    if (!ku_d3d_device_valid(device) || a == NULL || b == NULL || c == NULL) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    if (device->frontend == KU_D3D_FRONTEND_9 && device->scene_active == 0U) {
        return KU_STATUS_BAD_STATE;
    }
    clip = ku_d3d_effective_clip(device);
    if (clip.width <= 0 || clip.height <= 0) return KU_STATUS_OK;
    if (device->draw_calls >= device->draw_budget) return KU_STATUS_WOULD_BLOCK;
    status = ku_gfx_raster_triangle3d(
        device->target,
        device->depth_target,
        device->texture0,
        &clip,
        a, b, c,
        device->raster_budget - device->raster_pixels,
        &work);
    if (status == KU_STATUS_OK) {
        ++device->draw_calls;
        device->raster_pixels += work;
    }
    return status;
}

static inline ku_status_t ku_d3d_present(ku_d3d_device* device) {
    if (!ku_d3d_device_valid(device)) return KU_STATUS_INVALID_ARGUMENT;
    if (device->scene_active != 0U) return KU_STATUS_BAD_STATE;
    ++device->frame_index;
    device->draw_calls = 0U;
    device->raster_pixels = 0U;
    return KU_STATUS_OK;
}

static inline ku_status_t ku_d3d12_command_list_reset(
    ku_d3d12_command_list* list) {
    if (list == NULL) return KU_STATUS_INVALID_ARGUMENT;
    list->structure_size = sizeof(*list);
    list->count = 0U;
    return KU_STATUS_OK;
}

static inline ku_d3d12_command* ku_d3d12_allocate_command(
    ku_d3d12_command_list* list,
    uint32_t type) {
    ku_d3d12_command* command;
    if (list == NULL || list->structure_size != sizeof(*list) ||
        list->count >= KU_D3D12_MAX_COMMANDS) return NULL;
    command = &list->commands[list->count++];
    command->type = type;
    command->reserved = 0U;
    command->color = 0U;
    command->depth = 0U;
    return command;
}

static inline ku_status_t ku_d3d12_command_list_clear(
    ku_d3d12_command_list* list,
    ku_gfx_color_t color) {
    ku_d3d12_command* command = ku_d3d12_allocate_command(
        list, KU_D3D12_COMMAND_CLEAR);
    if (command == NULL) {
        return list == NULL || list->structure_size != sizeof(*list)
            ? KU_STATUS_INVALID_ARGUMENT : KU_STATUS_WOULD_BLOCK;
    }
    command->color = color;
    return KU_STATUS_OK;
}

static inline ku_status_t ku_d3d12_command_list_clear_depth(
    ku_d3d12_command_list* list,
    uint32_t depth) {
    ku_d3d12_command* command = ku_d3d12_allocate_command(
        list, KU_D3D12_COMMAND_CLEAR_DEPTH);
    if (command == NULL) {
        return list == NULL || list->structure_size != sizeof(*list)
            ? KU_STATUS_INVALID_ARGUMENT : KU_STATUS_WOULD_BLOCK;
    }
    command->depth = depth;
    return KU_STATUS_OK;
}

static inline ku_status_t ku_d3d12_command_list_triangle(
    ku_d3d12_command_list* list,
    const ku_gfx_vertex2d* a,
    const ku_gfx_vertex2d* b,
    const ku_gfx_vertex2d* c) {
    ku_d3d12_command* command;
    if (a == NULL || b == NULL || c == NULL) return KU_STATUS_INVALID_ARGUMENT;
    command = ku_d3d12_allocate_command(list, KU_D3D12_COMMAND_TRIANGLE);
    if (command == NULL) {
        return list == NULL || list->structure_size != sizeof(*list)
            ? KU_STATUS_INVALID_ARGUMENT : KU_STATUS_WOULD_BLOCK;
    }
    command->color = a->color;
    command->vertices[0] = *a;
    command->vertices[1] = *b;
    command->vertices[2] = *c;
    return KU_STATUS_OK;
}

static inline ku_status_t ku_d3d12_command_list_triangle3d(
    ku_d3d12_command_list* list,
    const ku_gfx_vertex3d* a,
    const ku_gfx_vertex3d* b,
    const ku_gfx_vertex3d* c) {
    ku_d3d12_command* command;
    if (a == NULL || b == NULL || c == NULL) return KU_STATUS_INVALID_ARGUMENT;
    command = ku_d3d12_allocate_command(list, KU_D3D12_COMMAND_TRIANGLE3D);
    if (command == NULL) {
        return list == NULL || list->structure_size != sizeof(*list)
            ? KU_STATUS_INVALID_ARGUMENT : KU_STATUS_WOULD_BLOCK;
    }
    command->vertices3d[0] = *a;
    command->vertices3d[1] = *b;
    command->vertices3d[2] = *c;
    return KU_STATUS_OK;
}

static inline ku_status_t ku_d3d12_execute(
    ku_d3d_device* device,
    const ku_d3d12_command_list* list) {
    uint32_t index;
    if (!ku_d3d_device_valid(device) || device->frontend != KU_D3D_FRONTEND_12 ||
        list == NULL || list->structure_size != sizeof(*list) ||
        list->count > KU_D3D12_MAX_COMMANDS) return KU_STATUS_INVALID_ARGUMENT;
    for (index = 0U; index < list->count; ++index) {
        const ku_d3d12_command* command = &list->commands[index];
        ku_status_t status;
        if (command->type == KU_D3D12_COMMAND_CLEAR) {
            status = ku_d3d_clear(device, command->color);
        } else if (command->type == KU_D3D12_COMMAND_TRIANGLE) {
            status = ku_d3d_draw_triangle(
                device,
                &command->vertices[0],
                &command->vertices[1],
                &command->vertices[2]);
        } else if (command->type == KU_D3D12_COMMAND_CLEAR_DEPTH) {
            status = ku_d3d_clear_depth(device, command->depth);
        } else if (command->type == KU_D3D12_COMMAND_TRIANGLE3D) {
            status = ku_d3d_draw_triangle3d(
                device,
                &command->vertices3d[0],
                &command->vertices3d[1],
                &command->vertices3d[2]);
        } else {
            return KU_STATUS_NOT_SUPPORTED;
        }
        if (status != KU_STATUS_OK) return status;
    }
    return KU_STATUS_OK;
}

#ifdef __cplusplus
}
#endif

#endif

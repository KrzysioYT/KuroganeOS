#ifndef KUROGANE_SDK_DIRECT3D_H
#define KUROGANE_SDK_DIRECT3D_H

#include <stddef.h>
#include <stdint.h>
#include <kurogane/graphics.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Kurogane Direct3D compatibility foundation.
 *
 * This is a source-level Kurogane API, not the Microsoft Windows COM ABI.
 * It gives D3D9/D3D11/D3D12 compatibility frontends one bounded software
 * backend today while the native present, shader and GPU backends are built.
 * Unsupported semantics are rejected rather than reported as successful.
 */
#define KU_D3D_COMPAT_ABI_VERSION UINT32_C(1)
#define KU_D3D12_MAX_COMMANDS UINT32_C(64)
#define KU_D3D_MAX_DRAWS_PER_FRAME UINT32_C(4096)

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
} ku_d3d_device;

enum ku_d3d12_command_type {
    KU_D3D12_COMMAND_CLEAR = 1,
    KU_D3D12_COMMAND_TRIANGLE = 2
};

typedef struct ku_d3d12_command {
    uint32_t type;
    uint32_t reserved;
    ku_gfx_color_t color;
    ku_gfx_vertex2d vertices[3];
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
    return KU_STATUS_OK;
}

static inline int ku_d3d_device_valid(const ku_d3d_device* device) {
    return device != NULL && device->structure_size == sizeof(*device) &&
        ku_d3d_frontend_valid(device->frontend) &&
        device->backend == KU_D3D_BACKEND_SOFTWARE &&
        ku_gfx_surface_valid(device->target) &&
        device->draw_budget != 0U &&
        device->draw_budget <= KU_D3D_MAX_DRAWS_PER_FRAME;
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
    if (!ku_d3d_device_valid(device)) return KU_STATUS_INVALID_ARGUMENT;
    return ku_gfx_clear(device->target, color);
}

static inline ku_status_t ku_d3d_draw_triangle(
    ku_d3d_device* device,
    const ku_gfx_vertex2d* a,
    const ku_gfx_vertex2d* b,
    const ku_gfx_vertex2d* c) {
    ku_status_t status;
    if (!ku_d3d_device_valid(device) || a == NULL || b == NULL || c == NULL) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    if (device->frontend == KU_D3D_FRONTEND_9 && device->scene_active == 0U) {
        return KU_STATUS_BAD_STATE;
    }
    if (device->draw_calls >= device->draw_budget) return KU_STATUS_WOULD_BLOCK;
    status = ku_gfx_fill_triangle(device->target, a, b, c);
    if (status == KU_STATUS_OK) ++device->draw_calls;
    return status;
}

/*
 * Finalize one bounded software frame. Window/swapchain presentation is a
 * separate native Kurogane Graphics operation and is intentionally not faked
 * here. The call resets per-frame work accounting.
 */
static inline ku_status_t ku_d3d_present(ku_d3d_device* device) {
    if (!ku_d3d_device_valid(device)) return KU_STATUS_INVALID_ARGUMENT;
    if (device->scene_active != 0U) return KU_STATUS_BAD_STATE;
    ++device->frame_index;
    device->draw_calls = 0U;
    return KU_STATUS_OK;
}

static inline ku_status_t ku_d3d12_command_list_reset(
    ku_d3d12_command_list* list) {
    if (list == NULL) return KU_STATUS_INVALID_ARGUMENT;
    list->structure_size = sizeof(*list);
    list->count = 0U;
    return KU_STATUS_OK;
}

static inline ku_status_t ku_d3d12_command_list_clear(
    ku_d3d12_command_list* list,
    ku_gfx_color_t color) {
    ku_d3d12_command* command;
    if (list == NULL || list->structure_size != sizeof(*list)) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    if (list->count >= KU_D3D12_MAX_COMMANDS) return KU_STATUS_WOULD_BLOCK;
    command = &list->commands[list->count++];
    command->type = KU_D3D12_COMMAND_CLEAR;
    command->reserved = 0U;
    command->color = color;
    return KU_STATUS_OK;
}

static inline ku_status_t ku_d3d12_command_list_triangle(
    ku_d3d12_command_list* list,
    const ku_gfx_vertex2d* a,
    const ku_gfx_vertex2d* b,
    const ku_gfx_vertex2d* c) {
    ku_d3d12_command* command;
    if (list == NULL || list->structure_size != sizeof(*list) ||
        a == NULL || b == NULL || c == NULL) return KU_STATUS_INVALID_ARGUMENT;
    if (list->count >= KU_D3D12_MAX_COMMANDS) return KU_STATUS_WOULD_BLOCK;
    command = &list->commands[list->count++];
    command->type = KU_D3D12_COMMAND_TRIANGLE;
    command->reserved = 0U;
    command->color = a->color;
    command->vertices[0] = *a;
    command->vertices[1] = *b;
    command->vertices[2] = *c;
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

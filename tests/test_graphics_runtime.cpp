#include <kurogane/direct3d.h>

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace {
constexpr uint32_t kCanary = UINT32_C(0xA55AA55A);
constexpr uint32_t kWidth = 64U;
constexpr uint32_t kHeight = 48U;
constexpr uint32_t kStride = 68U;
constexpr size_t kPixels = static_cast<size_t>(kStride) * kHeight;
uint32_t storage[kPixels + 2U]{};

void reset_storage() {
    for (size_t index = 0U; index < kPixels + 2U; ++index) {
        storage[index] = UINT32_C(0x00112233);
    }
    storage[0] = kCanary;
    storage[kPixels + 1U] = kCanary;
}

void assert_canaries() {
    assert(storage[0] == kCanary);
    assert(storage[kPixels + 1U] == kCanary);
}
} // namespace

int main() {
    reset_storage();

    ku_gfx_surface surface{};
    assert(ku_gfx_surface_init(
        &surface, &storage[1], kPixels, kWidth, kHeight, kStride) == KU_STATUS_OK);
    assert(ku_gfx_clear(&surface, ku_gfx_rgb(2U, 3U, 4U)) == KU_STATUS_OK);
    assert(storage[1] == UINT32_C(0x00020304));
    assert(storage[1U + static_cast<size_t>(kHeight - 1U) * kStride + kWidth - 1U] ==
        UINT32_C(0x00020304));
    assert_canaries();

    /* Clipped work must never write before/after the caller-owned surface. */
    assert(ku_gfx_fill_rect(
        &surface, -100, -100, 130, 130, ku_gfx_rgb(255U, 0U, 0U)) ==
        KU_STATUS_OK);
    assert(ku_gfx_draw_line(
        &surface, -20, -20, 100, 80, ku_gfx_rgb(0U, 255U, 0U)) ==
        KU_STATUS_OK);
    assert_canaries();

    const ku_gfx_vertex2d a{8, 6, ku_gfx_rgb(255U, 128U, 0U)};
    const ku_gfx_vertex2d b{56, 8, ku_gfx_rgb(255U, 128U, 0U)};
    const ku_gfx_vertex2d c{30, 42, ku_gfx_rgb(255U, 128U, 0U)};
    assert(ku_gfx_fill_triangle(&surface, &a, &b, &c) == KU_STATUS_OK);
    assert(storage[1U + static_cast<size_t>(20U) * kStride + 30U] ==
        ku_gfx_rgb(255U, 128U, 0U));
    assert_canaries();

    ku_d3d_device d3d9{};
    assert(ku_d3d_create_software_device(&d3d9, KU_D3D_FRONTEND_9, &surface) ==
        KU_STATUS_OK);
    assert(ku_d3d_draw_triangle(&d3d9, &a, &b, &c) == KU_STATUS_BAD_STATE);
    assert(ku_d3d_begin_scene(&d3d9) == KU_STATUS_OK);
    assert(ku_d3d_draw_triangle(&d3d9, &a, &b, &c) == KU_STATUS_OK);
    assert(ku_d3d_end_scene(&d3d9) == KU_STATUS_OK);
    assert(ku_d3d_present(&d3d9) == KU_STATUS_OK);
    assert(d3d9.frame_index == 1U && d3d9.draw_calls == 0U);

    ku_d3d_device d3d11{};
    assert(ku_d3d_create_software_device(&d3d11, KU_D3D_FRONTEND_11, &surface) ==
        KU_STATUS_OK);
    d3d11.draw_budget = 1U;
    assert(ku_d3d_draw_triangle(&d3d11, &a, &b, &c) == KU_STATUS_OK);
    assert(ku_d3d_draw_triangle(&d3d11, &a, &b, &c) == KU_STATUS_WOULD_BLOCK);
    assert(ku_d3d_present(&d3d11) == KU_STATUS_OK);

    ku_d3d_device d3d12{};
    ku_d3d12_command_list list{};
    assert(ku_d3d_create_software_device(&d3d12, KU_D3D_FRONTEND_12, &surface) ==
        KU_STATUS_OK);
    assert(ku_d3d12_command_list_reset(&list) == KU_STATUS_OK);
    assert(ku_d3d12_command_list_clear(&list, ku_gfx_rgb(0U, 0U, 16U)) ==
        KU_STATUS_OK);
    assert(ku_d3d12_command_list_triangle(&list, &a, &b, &c) == KU_STATUS_OK);
    assert(ku_d3d12_execute(&d3d12, &list) == KU_STATUS_OK);
    assert(ku_d3d_present(&d3d12) == KU_STATUS_OK);
    assert_canaries();

    /* Invalid surface arithmetic must be rejected before any memory access. */
    ku_gfx_surface invalid{};
    invalid.structure_size = sizeof(invalid);
    invalid.format = KU_GFX_FORMAT_XRGB8888;
    invalid.width = KU_GFX_MAX_DIMENSION;
    invalid.height = KU_GFX_MAX_DIMENSION;
    invalid.stride_pixels = KU_GFX_MAX_DIMENSION;
    invalid.pixels = &storage[1];
    invalid.pixel_capacity = 1U;
    assert(!ku_gfx_surface_valid(&invalid));

    return 0;
}

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
uint32_t depth_storage[kPixels + 2U]{};

void reset_storage() {
    for (size_t index = 0U; index < kPixels + 2U; ++index) {
        storage[index] = UINT32_C(0x00112233);
        depth_storage[index] = KU_GFX_DEPTH_FAR;
    }
    storage[0] = kCanary;
    storage[kPixels + 1U] = kCanary;
    depth_storage[0] = kCanary;
    depth_storage[kPixels + 1U] = kCanary;
}

void assert_canaries() {
    assert(storage[0] == kCanary);
    assert(storage[kPixels + 1U] == kCanary);
    assert(depth_storage[0] == kCanary);
    assert(depth_storage[kPixels + 1U] == kCanary);
}

size_t pixel_index(uint32_t x, uint32_t y) {
    return 1U + static_cast<size_t>(y) * kStride + x;
}
} // namespace

int main() {
    static_assert(KU_GFX_ABI_VERSION == 2U);
    static_assert(KU_D3D_COMPAT_ABI_VERSION == 2U);
    static_assert(sizeof(ku_gfx_vertex3d) == 24U);

    reset_storage();

    ku_gfx_surface surface{};
    ku_gfx_depth_surface depth{};
    assert(ku_gfx_surface_init(
        &surface, &storage[1], kPixels, kWidth, kHeight, kStride) == KU_STATUS_OK);
    assert(ku_gfx_depth_surface_init(
        &depth, &depth_storage[1], kPixels, kWidth, kHeight, kStride) == KU_STATUS_OK);
    assert(ku_gfx_clear(&surface, ku_gfx_rgb(2U, 3U, 4U)) == KU_STATUS_OK);
    assert(ku_gfx_clear_depth(&depth, KU_GFX_DEPTH_FAR) == KU_STATUS_OK);
    assert(storage[1] == UINT32_C(0x00020304));
    assert(storage[pixel_index(kWidth - 1U, kHeight - 1U)] == UINT32_C(0x00020304));
    assert(depth_storage[pixel_index(0U, 0U)] == KU_GFX_DEPTH_FAR);
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
    assert(storage[pixel_index(30U, 20U)] == ku_gfx_rgb(255U, 128U, 0U));
    assert_canaries();

    /* D3D9 retains BeginScene/EndScene semantics and bounded work accounting. */
    ku_d3d_device d3d9{};
    assert(ku_d3d_create_software_device(&d3d9, KU_D3D_FRONTEND_9, &surface) ==
        KU_STATUS_OK);
    assert(ku_d3d_draw_triangle(nullptr, &a, &b, &c) == KU_STATUS_INVALID_ARGUMENT);
    assert(ku_d3d_draw_triangle(&d3d9, &a, &b, &c) == KU_STATUS_BAD_STATE);
    assert(ku_d3d_begin_scene(&d3d9) == KU_STATUS_OK);
    assert(ku_d3d_draw_triangle(&d3d9, &a, &b, &c) == KU_STATUS_OK);
    assert(d3d9.raster_pixels != 0U);
    assert(ku_d3d_end_scene(&d3d9) == KU_STATUS_OK);
    assert(ku_d3d_present(&d3d9) == KU_STATUS_OK);
    assert(d3d9.frame_index == 1U && d3d9.draw_calls == 0U &&
        d3d9.raster_pixels == 0U);

    ku_d3d_device d3d11{};
    assert(ku_d3d_create_software_device(&d3d11, KU_D3D_FRONTEND_11, &surface) ==
        KU_STATUS_OK);
    d3d11.draw_budget = 1U;
    assert(ku_d3d_draw_triangle(&d3d11, &a, &b, &c) == KU_STATUS_OK);
    assert(ku_d3d_draw_triangle(&d3d11, &a, &b, &c) == KU_STATUS_WOULD_BLOCK);
    assert(ku_d3d_present(&d3d11) == KU_STATUS_OK);

    /* Texture2D + depth + viewport/scissor use fixed-point, bounded software rasterization. */
    const uint32_t texture_pixels[4] = {
        UINT32_C(0x00FF0000), UINT32_C(0x0000FF00),
        UINT32_C(0x000000FF), UINT32_C(0x00FFFFFF)
    };
    ku_gfx_texture2d texture{};
    assert(ku_gfx_texture2d_init(&texture, texture_pixels, 4U, 2U, 2U, 2U) ==
        KU_STATUS_OK);

    assert(ku_gfx_clear(&surface, UINT32_C(0x00000000)) == KU_STATUS_OK);
    assert(ku_gfx_clear_depth(&depth, KU_GFX_DEPTH_FAR) == KU_STATUS_OK);
    ku_d3d_device textured{};
    assert(ku_d3d_create_software_device(&textured, KU_D3D_FRONTEND_11, &surface) ==
        KU_STATUS_OK);
    assert(ku_d3d_set_depth_surface(&textured, &depth) == KU_STATUS_OK);
    assert(ku_d3d_set_texture0(&textured, &texture) == KU_STATUS_OK);
    const ku_gfx_rect viewport{0, 0, static_cast<int32_t>(kWidth), static_cast<int32_t>(kHeight)};
    const ku_gfx_rect scissor{16, 12, 32, 24};
    assert(ku_d3d_set_viewport(&textured, &viewport) == KU_STATUS_OK);
    assert(ku_d3d_set_scissor(&textured, &scissor) == KU_STATUS_OK);

    const ku_gfx_vertex3d ta{8, 6, UINT32_C(0x70000000), 0, 0, UINT32_C(0x00FFFFFF)};
    const ku_gfx_vertex3d tb{56, 8, UINT32_C(0x70000000), 0, 0, UINT32_C(0x00FFFFFF)};
    const ku_gfx_vertex3d tc{30, 42, UINT32_C(0x70000000), 0, 0, UINT32_C(0x00FFFFFF)};
    assert(ku_d3d_draw_triangle3d(&textured, &ta, &tb, &tc) == KU_STATUS_OK);
    assert(storage[pixel_index(30U, 20U)] == UINT32_C(0x00FF0000));
    assert(storage[pixel_index(10U, 10U)] == UINT32_C(0x00000000));

    assert(ku_d3d_set_texture0(&textured, nullptr) == KU_STATUS_OK);
    const ku_gfx_vertex3d na{8, 6, UINT32_C(0x10000000), 0, 0, UINT32_C(0x0000FF00)};
    const ku_gfx_vertex3d nb{56, 8, UINT32_C(0x10000000), 0, 0, UINT32_C(0x0000FF00)};
    const ku_gfx_vertex3d nc{30, 42, UINT32_C(0x10000000), 0, 0, UINT32_C(0x0000FF00)};
    assert(ku_d3d_draw_triangle3d(&textured, &na, &nb, &nc) == KU_STATUS_OK);
    assert(storage[pixel_index(30U, 20U)] == UINT32_C(0x0000FF00));

    const ku_gfx_vertex3d fa{8, 6, UINT32_C(0xF0000000), 0, 0, UINT32_C(0x000000FF)};
    const ku_gfx_vertex3d fb{56, 8, UINT32_C(0xF0000000), 0, 0, UINT32_C(0x000000FF)};
    const ku_gfx_vertex3d fc{30, 42, UINT32_C(0xF0000000), 0, 0, UINT32_C(0x000000FF)};
    assert(ku_d3d_draw_triangle3d(&textured, &fa, &fb, &fc) == KU_STATUS_OK);
    assert(storage[pixel_index(30U, 20U)] == UINT32_C(0x0000FF00));
    assert_canaries();

    /* A frame can explicitly lower its CPU budget; oversized raster work is rejected atomically. */
    assert(ku_d3d_present(&textured) == KU_STATUS_OK);
    assert(ku_d3d_set_frame_budgets(&textured, 8U, 100U) == KU_STATUS_OK);
    assert(ku_d3d_draw_triangle3d(&textured, &na, &nb, &nc) == KU_STATUS_WOULD_BLOCK);
    assert(textured.raster_pixels == 0U);

    /* D3D12-style retained commands include color, depth and 3D draw operations. */
    assert(ku_gfx_clear(&surface, UINT32_C(0x00000000)) == KU_STATUS_OK);
    assert(ku_gfx_clear_depth(&depth, KU_GFX_DEPTH_FAR) == KU_STATUS_OK);
    ku_d3d_device d3d12{};
    ku_d3d12_command_list list{};
    assert(ku_d3d_create_software_device(&d3d12, KU_D3D_FRONTEND_12, &surface) ==
        KU_STATUS_OK);
    assert(ku_d3d_set_depth_surface(&d3d12, &depth) == KU_STATUS_OK);
    assert(ku_d3d12_command_list_reset(&list) == KU_STATUS_OK);
    assert(ku_d3d12_command_list_clear(&list, ku_gfx_rgb(0U, 0U, 16U)) ==
        KU_STATUS_OK);
    assert(ku_d3d12_command_list_clear_depth(&list, KU_GFX_DEPTH_FAR) == KU_STATUS_OK);
    assert(ku_d3d12_command_list_triangle3d(&list, &na, &nb, &nc) == KU_STATUS_OK);
    assert(ku_d3d12_execute(&d3d12, &list) == KU_STATUS_OK);
    assert(storage[pixel_index(30U, 20U)] == UINT32_C(0x0000FF00));
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

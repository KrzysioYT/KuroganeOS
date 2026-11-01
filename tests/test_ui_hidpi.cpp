#include "../kernel/drivers/framebuffer.hpp"
#include "../kernel/ui/ui.hpp"
#include <kurogane/design.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {
constexpr uint32_t WIDTH = 600U;
constexpr uint32_t HEIGHT = 240U;
uint32_t g_pixels[WIDTH * HEIGHT]{};

struct IconExtent {
    int32_t max_x;
    int32_t max_y;
};

IconExtent draw_home_icon(uint32_t scale_percent, uint32_t style) {
    assert(graphics::set_text_scale_percent(scale_percent));
    assert(ui::set_desktop_style(style));
    graphics::clear(0U);
    const int32_t tile_width = static_cast<int32_t>(
        (static_cast<uint64_t>(KU_FLUX_TILE_WIDTH) * scale_percent + 50U) / 100U);
    const int32_t tile_height = static_cast<int32_t>(
        (static_cast<uint64_t>(KU_FLUX_TILE_HEIGHT) * scale_percent + 50U) / 100U);
    constexpr int32_t origin_x = 20;
    constexpr int32_t origin_y = 20;
    ui::app_tile(
        {origin_x, origin_y, tile_width, tile_height},
        "", "", ui::AppIcon::Home);

    const ui::Theme& theme = ui::default_theme();
    const int32_t text_start = static_cast<int32_t>(
        (50U * scale_percent + 50U) / 100U);
    IconExtent extent{-1, -1};
    for (int32_t y = 0; y < tile_height; ++y) {
        for (int32_t x = 0; x < text_start; ++x) {
            const size_t offset =
                static_cast<size_t>(origin_y + y) * WIDTH +
                static_cast<size_t>(origin_x + x);
            const uint32_t pixel = g_pixels[offset] & UINT32_C(0x00FFFFFF);
            if (pixel != theme.text && pixel != theme.accent) continue;
            if (x > extent.max_x) extent.max_x = x;
            if (y > extent.max_y) extent.max_y = y;
        }
    }
    return extent;
}
}

int main() {
    KuroganeFramebuffer framebuffer{};
    framebuffer.base = g_pixels;
    framebuffer.width = WIDTH;
    framebuffer.height = HEIGHT;
    framebuffer.pitch = WIDTH * sizeof(uint32_t);
    framebuffer.bpp = 32U;
    framebuffer.pixel_format = KUROGANE_PIXEL_BGRX8;
    assert(graphics::init(framebuffer));
    assert(ui::set_desktop_theme(KU_FLUX_THEME_RED));
    assert(ui::set_desktop_style(KU_FLUX_STYLE_MODERN));

    constexpr uint32_t scales[] = {100U, 125U, 150U, 200U};
    constexpr uint32_t styles[] = {KU_FLUX_STYLE_MODERN, KU_FLUX_STYLE_CLASSIC};
    for (const uint32_t style : styles) {
        const IconExtent standard = draw_home_icon(100U, style);
        assert(standard.max_x > 0 && standard.max_y > 0);
        for (const uint32_t scale : scales) {
            const IconExtent extent = draw_home_icon(scale, style);
            const int32_t expected_x = static_cast<int32_t>(
                (static_cast<int64_t>(standard.max_x) * scale + 50) / 100);
            const int32_t expected_y = static_cast<int32_t>(
                (static_cast<int64_t>(standard.max_y) * scale + 50) / 100);
            assert(extent.max_x >= expected_x - 1);
            assert(extent.max_x <= expected_x + 1);
            assert(extent.max_y >= expected_y - 1);
            assert(extent.max_y <= expected_y + 1);
        }
    }
    assert(graphics::set_text_scale_percent(100U));
    std::puts("Flux native application icon HiDPI rendering: PASS");
    return 0;
}

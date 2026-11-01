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

IconExtent draw_home_icon(uint32_t scale_percent) {
    assert(graphics::set_text_scale_percent(scale_percent));
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

    const IconExtent standard = draw_home_icon(100U);
    const IconExtent doubled = draw_home_icon(200U);
    assert(standard.max_x > 0 && standard.max_y > 0);
    assert(doubled.max_x >= standard.max_x * 2);
    assert(doubled.max_x <= standard.max_x * 2 + 1);
    assert(doubled.max_y >= standard.max_y * 2);
    assert(doubled.max_y <= standard.max_y * 2 + 1);
    assert(graphics::set_text_scale_percent(100U));
    std::puts("Flux native application icon HiDPI rendering: PASS");
    return 0;
}

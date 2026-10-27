#include "../kernel/ui/ui.hpp"
#include <kurogane/design.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {
constexpr uint32_t WIDTH = 1024U;
constexpr uint32_t HEIGHT = 720U;
static uint32_t g_pixels[static_cast<size_t>(WIDTH) * HEIGHT]{};

uint64_t interior_signature() {
    uint64_t result = UINT64_C(14695981039346656037);
    for (uint32_t y = 90U; y < HEIGHT - 60U; y += 11U) {
        for (uint32_t x = 110U; x < WIDTH - 20U; x += 11U) {
            result ^= g_pixels[static_cast<size_t>(y) * WIDTH + x];
            result *= UINT64_C(1099511628211);
        }
    }
    return result;
}

uint64_t render(uint32_t wallpaper) {
    assert(ui::set_desktop_background(wallpaper));
    assert(graphics::begin_frame());
    ui::desktop("KUROGANE / RED FLUX");
    graphics::end_frame();
    return interior_signature();
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

    const uint64_t forged = render(KU_FLUX_BACKGROUND_FORGED);
    const uint64_t grid = render(KU_FLUX_BACKGROUND_GRID);
    assert(g_pixels[300U * WIDTH + 32U] == ui::default_theme().panel);
    const uint64_t minimal = render(KU_FLUX_BACKGROUND_MINIMAL);
    assert(g_pixels[300U * WIDTH + 32U] == ui::default_theme().desktop);

    assert(forged != grid);
    assert(grid != minimal);
    assert(forged != minimal);
    assert(ui::set_desktop_background(KU_FLUX_BACKGROUND_FORGED));
    std::puts("Flux 8 framebuffer wallpaper rendering: PASS");
    return 0;
}

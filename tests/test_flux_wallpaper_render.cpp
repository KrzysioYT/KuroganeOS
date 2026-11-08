#include "../kernel/ui/ui.hpp"
#include <kurogane/design.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

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

void verify_session_header() {
    constexpr size_t header_height = 57U;
    static uint32_t offline[WIDTH * header_height];
    for (const uint32_t scale : {100U, 125U, 150U, 200U}) {
        assert(graphics::set_text_scale_percent(scale));
        for (const bool ready : {false, true}) {
            assert(graphics::begin_frame());
            ui::desktop("KUROGANE / RED FLUX");
            ui::desktop_session_status(ready);
            graphics::end_frame();
            if (!ready) {
                for (size_t i = 0; i < WIDTH * header_height; ++i) offline[i] = g_pixels[i];
            }
        }
        const uint32_t label = graphics::measure_text_width("DESKTOP SESSION:");
        const uint32_t value = graphics::measure_text_width("OFFLINE");
        const uint32_t value_x = (WIDTH - label - value - 21U) / 2U + label + 8U;
        size_t changed = 0U;
        for (size_t y = 0; y < header_height; ++y) {
            for (size_t x = 0; x < WIDTH; ++x) {
                if (g_pixels[y * WIDTH + x] == offline[y * WIDTH + x]) continue;
                // The label and its separation gap are unchanged when the
                // connection status changes, even with the largest glyphs.
                assert(x >= value_x);
                ++changed;
            }
        }
        assert(changed > 20U);
    }
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
    verify_session_header();
    std::puts("Flux 8 framebuffer wallpaper rendering: PASS");
    return 0;
}

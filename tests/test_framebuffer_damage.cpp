#include "../kernel/drivers/framebuffer.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {
constexpr uint32_t WIDTH = 16U;
constexpr uint32_t HEIGHT = 12U;
uint32_t g_pixels[WIDTH * HEIGHT]{};

bool equals(uint32_t x, uint32_t y, uint32_t value) {
    return g_pixels[static_cast<size_t>(y) * WIDTH + x] == value;
}

bool all_equal(uint32_t value) {
    for (uint32_t pixel : g_pixels) {
        if (pixel != value) return false;
    }
    return true;
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
    if (!graphics::init(framebuffer)) return 1;

    constexpr uint32_t baseline = 0x00112233U;
    constexpr uint32_t changed = 0x00445566U;
    constexpr uint32_t clipped = 0x00778899U;
    if (!graphics::begin_frame()) return 2;
    graphics::clear(baseline);
    graphics::end_frame();
    if (!all_equal(baseline)) return 3;

    const graphics::DamageRect damage[] = {
        {3, 2, 4, 3},
        {10, 7, 3, 2},
    };
    if (!graphics::begin_frame()) return 4;
    if (!graphics::set_damage_regions(damage, 2U) ||
        !graphics::damage_regions_active()) return 5;

    // clear() and reset_clip() must never escape the outer compositor mask.
    graphics::clear(changed);
    graphics::set_clip(4, 3, 1, 1);
    graphics::fill_rect(0, 0, static_cast<int32_t>(WIDTH),
                        static_cast<int32_t>(HEIGHT), clipped);
    graphics::reset_clip();
    graphics::fill_rect(0, 0, static_cast<int32_t>(WIDTH),
                        static_cast<int32_t>(HEIGHT), changed);
    graphics::put_pixel(0, 0, clipped);
    graphics::end_frame_regions(damage, 2U);

    for (uint32_t y = 0U; y < HEIGHT; ++y) {
        for (uint32_t x = 0U; x < WIDTH; ++x) {
            const bool damaged =
                (x >= 3U && x < 7U && y >= 2U && y < 5U) ||
                (x >= 10U && x < 13U && y >= 7U && y < 9U);
            if (!equals(x, y, damaged ? changed : baseline)) return 6;
        }
    }

    // A later full present exposes the whole retained backbuffer.  This proves
    // the partial raster itself, not merely the final GOP blit, stayed bounded.
    if (!graphics::begin_frame()) return 7;
    graphics::end_frame();
    for (uint32_t y = 0U; y < HEIGHT; ++y) {
        for (uint32_t x = 0U; x < WIDTH; ++x) {
            const bool damaged =
                (x >= 3U && x < 7U && y >= 2U && y < 5U) ||
                (x >= 10U && x < 13U && y >= 7U && y < 9U);
            if (!equals(x, y, damaged ? changed : baseline)) return 8;
        }
    }

    // Damage-aware scroll may read outside the damage region but may write only
    // inside it.  The subsequent full present again exposes hidden corruption.
    const graphics::DamageRect scroll_damage = {5, 4, 2, 4};
    if (!graphics::begin_frame()) return 9;
    if (!graphics::set_damage_regions(&scroll_damage, 1U)) return 10;
    graphics::scroll_up(1U, clipped);
    graphics::end_frame_regions(&scroll_damage, 1U);
    if (!graphics::begin_frame()) return 11;
    graphics::end_frame();
    for (uint32_t y = 0U; y < HEIGHT; ++y) {
        for (uint32_t x = 0U; x < WIDTH; ++x) {
            const bool first_damage =
                (x >= 3U && x < 7U && y >= 2U && y < 5U) ||
                (x >= 10U && x < 13U && y >= 7U && y < 9U);
            if (x < 5U || x >= 7U || y < 4U || y >= 8U) {
                if (!equals(x, y, first_damage ? changed : baseline)) return 12;
            }
        }
    }

    graphics::DamageRect too_many[graphics::MAX_COMPOSITOR_DAMAGE_REGIONS + 1U]{};
    if (graphics::set_damage_regions(
            too_many, graphics::MAX_COMPOSITOR_DAMAGE_REGIONS + 1U) ||
        graphics::damage_regions_active()) return 13;

    std::puts("framebuffer compositor damage raster tests passed");
    return 0;
}

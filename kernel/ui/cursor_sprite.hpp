#pragma once
#include <stdint.h>

namespace flux_cursor {
constexpr int32_t kWidth = 16;
constexpr int32_t kHeight = 21;
enum class Ink : uint8_t { Transparent, Outline, Fill, Edge, Shadow };

/* No external image/allocator. Hotspot is (0,0), no interpolation of the
 * actual pointer coordinates: input events must keep their precise timing.
 * The cursor is a compositor overlay; hit-testing uses input::pointer_x/y.
 */
constexpr Ink sample(int32_t x, int32_t y) {
    if (x < 0 || x >= kWidth || y < 0 || y >= kHeight)
        return Ink::Transparent;
    if (y < 14) {
        const int32_t limit = y + 1;
        if (x == limit && y > 1) return Ink::Shadow;
        if (x >= limit) return Ink::Transparent;
        if (x == 0 || x == limit - 1 || y == 13) return Ink::Outline;
        if (x == limit - 2) return Ink::Edge;
        return Ink::Fill;
    }
    if (y <= 19) {
        const int32_t left = 4 + (y - 14) / 2;
        const int32_t right = left + 3;
        if (x == right + 1 && y < 19) return Ink::Shadow;
        if (x < left || x > right) return Ink::Transparent;
        if (x == left || x == right || y == 19) return Ink::Outline;
        return Ink::Fill;
    }
    return Ink::Transparent;
}

/* All additions happen at int64 precision; clipped edges never overflow i32.
 * Visitor receives logical raster index, native screen x/y, and ink.
 */
template <typename Visitor>
inline void visit_visible(
    int32_t x, int32_t y, uint32_t screen_width, uint32_t screen_height,
    Visitor visitor) {
    for (int32_t cy = 0; cy < kHeight; ++cy) {
        for (int32_t cx = 0; cx < kWidth; ++cx) {
            const Ink ink = sample(cx, cy);
            if (ink == Ink::Transparent) continue;
            const int64_t sx = static_cast<int64_t>(x) + cx;
            const int64_t sy = static_cast<int64_t>(y) + cy;
            if (sx < 0 || sy < 0 ||
                sx >= static_cast<int64_t>(screen_width) ||
                sy >= static_cast<int64_t>(screen_height)) continue;
            visitor(static_cast<uint32_t>(cy * kWidth + cx),
                    static_cast<int32_t>(sx), static_cast<int32_t>(sy), ink);
        }
    }
}
} // namespace flux_cursor

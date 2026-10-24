#pragma once
#include <stdint.h>

namespace graphics {
namespace raster {
/* Disk intersection for one scanline of a rounded corner. Pure integer
 * geometry: deterministic in freestanding kernel builds and host UBSan.
 * Caller clamps radius to <= 16 and rectangle dimensions.
 */
constexpr int32_t rounded_inset(int32_t radius, int32_t row) {
    if (radius <= 1 || row < 0 || row >= radius) return 0;
    const int32_t center = radius - 1;
    const int32_t dy = center - row;
    int32_t inset = radius;
    for (int32_t x = center; x >= 0; --x) {
        const int32_t dx = center - x;
        if (dx * dx + dy * dy > center * center) break;
        inset = x;
    }
    return inset;
}
// Four-by-four subpixel coverage of a rounded corner. No floating point.
// The compositor mirrors these coordinates across the other three corners.
constexpr uint8_t coverage4x4(int32_t radius, int32_t x, int32_t y) {
    if (radius <= 0 || x < 0 || y < 0 || x >= radius || y >= radius) return 0U;
    const int32_t center = radius * 8;
    const int32_t squared = center * center;
    uint8_t count = 0U;
    for (int32_t iy = 0; iy < 4; ++iy) {
        const int32_t dy = center - (y * 8 + iy * 2 + 1);
        for (int32_t ix = 0; ix < 4; ++ix) {
            const int32_t dx = center - (x * 8 + ix * 2 + 1);
            if (dx * dx + dy * dy <= squared) ++count;
        }
    }
    return count;
}
} // namespace raster
} // namespace graphics

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
} // namespace raster
} // namespace graphics

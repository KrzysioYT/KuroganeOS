#pragma once
#include <stddef.h>
#include <stdint.h>
// Signed wheel steps; keep page index aligned and bounded, even for INT32_MIN.
static inline size_t flux_files_wheel_page(size_t first, size_t count,
                                           size_t page_size, int32_t wheel_y) {
    if (page_size == 0U || count == 0U || wheel_y == 0) return first;
    const size_t last_page = (count - 1U) / page_size;
    size_t page = first / page_size;
    if (page > last_page) page = last_page;
    if (wheel_y < 0) {
        const uint64_t steps = (uint64_t)(-(int64_t)wheel_y);
        const size_t distance = last_page - page;
        page = steps >= distance ? last_page : page + (size_t)steps;
    } else {
        const uint64_t steps = (uint64_t)wheel_y;
        page = steps >= page ? 0U : page - (size_t)steps;
    }
    return page * page_size;
}

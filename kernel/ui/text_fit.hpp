#pragma once
#include <stddef.h>
#include <stdint.h>

// Render-safe ASCII fallback: bound the output to the available cell width.
// No heap, no UTF-8 truncation claim (future font rasterizer handles Unicode).
namespace ui::text_fit {
inline void fit_ascii(const char* input, char* output, size_t capacity,
                      size_t max_columns) {
    if (output == nullptr || capacity == 0U) return;
    output[0] = '\0';
    if (input == nullptr || max_columns == 0U) return;
    size_t limit = capacity - 1U;
    if (max_columns < limit) limit = max_columns;
    if (limit == 0U) return;
    size_t n = 0U;
    while (n < limit && input[n] != '\0' &&
           input[n] != '\n' && input[n] != '\r') {
        output[n] = input[n];
        ++n;
    }
    const bool truncated = n == limit &&
        input[n] != '\0' && input[n] != '\n' && input[n] != '\r';
    if (truncated && limit >= 4U) {
        output[limit-3U] = '.';
        output[limit-2U] = '.';
        output[limit-1U] = '.';
        n = limit;
    }
    output[n] = '\0';
}
}

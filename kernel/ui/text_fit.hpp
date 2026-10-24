#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../libk/utf8_text.hpp"

namespace ui::text_fit {
// Count a UTF-8 scalar as one cell; invalid sequences become '?'. Truncate
// at a code point boundary, never producing an unterminated / malformed
// multi-byte string. Width = monospaced cell count (not proportional TTF).
inline void fit_utf8(const char* input, char* output, size_t capacity,
                     size_t max_columns) {
    if (output == nullptr || capacity == 0U) return;
    output[0] = '\0';
    if (input == nullptr || max_columns == 0U) return;

    // Runtime UI buffers are small; a bounded lookahead avoids traversing
    // potentially unbounded names merely to decide whether to show "...".
    size_t total_columns = 0U;
    size_t total_bytes = 0U;
    const char* walk = input;
    while (*walk != '\0' && *walk != '\n' && *walk != '\r' &&
           total_columns <= max_columns && total_bytes < capacity) {
        const ku_text::Scalar scalar = ku_text::next_z(walk);
        if (scalar.bytes == 0U) break;
        const size_t bytes = scalar.valid ? scalar.bytes : 1U;
        if (total_bytes > capacity - 1U ||
            bytes > capacity - 1U - total_bytes) {
            total_bytes = capacity;
            break;
        }
        total_bytes += bytes;
        ++total_columns;
        walk += scalar.bytes;
    }
    const bool more = (*walk != '\0' && *walk != '\n' && *walk != '\r');
    const bool truncated = more || total_columns > max_columns ||
                           total_bytes >= capacity;
    const size_t dots = truncated && max_columns >= 4U && capacity >= 4U
        ? 3U : 0U;
    const size_t visible = max_columns - dots;
    size_t columns = 0U, used = 0U;
    walk = input;
    while (columns < visible && *walk != '\0' &&
           *walk != '\n' && *walk != '\r') {
        const ku_text::Scalar scalar = ku_text::next_z(walk);
        if (scalar.bytes == 0U) break;
        const size_t bytes = scalar.valid ? scalar.bytes : 1U;
        if (used >= capacity - 1U ||
            bytes > capacity - 1U - used - dots) break;
        if (scalar.valid) {
            for (size_t i = 0U; i < bytes; ++i) output[used++] = walk[i];
        } else {
            output[used++] = '?';
        }
        walk += scalar.bytes;
        ++columns;
    }
    if (dots != 0U) {
        for (size_t i = 0U; i < dots; ++i) output[used++] = '.';
    }
    output[used] = '\0';
}

// ABI/source-compatible wrapper: ASCII and UTF-8 both use one display cell
// per scalar. Existing callers gain correct Unicode boundary handling.
inline void fit_ascii(const char* input, char* output, size_t capacity,
                      size_t max_columns) {
    fit_utf8(input, output, capacity, max_columns);
}
}

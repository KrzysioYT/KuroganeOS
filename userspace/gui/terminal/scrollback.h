#ifndef KUROGANE_TERMINAL_SCROLLBACK_H
#define KUROGANE_TERMINAL_SCROLLBACK_H

#include <stddef.h>
#include <stdint.h>

/* Offset is in lines from the newest viewport. Positive steps go backward
 * in history. Widen the arithmetic before accepting wheel detents. */
static inline size_t terminal_scrollback_step(
    size_t offset, size_t count, size_t visible, int64_t steps) {
    const size_t maximum = count > visible ? count - visible : 0U;
    if (offset > maximum) offset = maximum;
    if (steps > 0) {
        const uint64_t distance = (uint64_t)steps;
        const size_t room = maximum - offset;
        return distance >= (uint64_t)room ? maximum : offset + (size_t)distance;
    }
    if (steps < 0) {
        const uint64_t distance = (uint64_t)(-(steps + 1)) + 1U;
        return distance >= (uint64_t)offset ? 0U : offset - (size_t)distance;
    }
    return offset;
}

#endif

#include "../kernel/user/ui_event_queue.hpp"

#include <stdint.h>
#include <stdio.h>

namespace {
ku_ui_event event(uint32_t type, uint32_t buttons = 0U, int32_t x = 0,
                  int32_t wheel = 0) {
    ku_ui_event v{};
    v.structure_size = sizeof(v);
    v.type = type;
    v.buttons = buttons;
    v.x = x;
    if (type == KU_UI_EVENT_SCROLL) v.wheel_y = wheel;
    return v;
}
} // namespace

int main() {
    using namespace user::ui_event_queue;
    Queue queue{};
    ku_ui_event out{};
    if (pop(queue, &out) || pending(queue) != 0U) return 1;

    // Hundreds of relative mouse samples must not starve UI input.
    for (int32_t x = 1; x <= 600; ++x) {
        if (!push(queue, event(KU_UI_EVENT_POINTER, 0U, x))) return 2;
    }
    if (pending(queue) != 1U || !pop(queue, &out) ||
        out.x != 600 || out.buttons != 0U || pop(queue, &out)) return 3;

    // Buttons are edges: press, held movement and release remain ordered.
    if (!push(queue, event(KU_UI_EVENT_POINTER, 1U, 10)) ||
        !push(queue, event(KU_UI_EVENT_POINTER, 1U, 20)) ||
        !push(queue, event(KU_UI_EVENT_POINTER, 0U, 30)) ||
        pending(queue) != 2U) return 4;
    if (!pop(queue, &out) || out.buttons != 1U || out.x != 20 ||
        !pop(queue, &out) || out.buttons != 0U || out.x != 30) return 5;

    // Positive/negative wheel pulses are merged only within their sign.
    if (!push(queue, event(KU_UI_EVENT_SCROLL, 0U, 12, -3)) ||
        !push(queue, event(KU_UI_EVENT_SCROLL, 0U, 12, -4)) ||
        !push(queue, event(KU_UI_EVENT_SCROLL, 0U, 15, 2)) ||
        pending(queue) != 2U) return 6;
    if (!pop(queue, &out) || out.wheel_y != -7 || out.x != 12 ||
        !pop(queue, &out) || out.wheel_y != 2 || out.x != 15) return 7;

    // Full queue of keys must never be overwritten by incidental motion.
    for (unsigned i = 0; i < kCapacity - 1U; ++i) {
        if (!push(queue, event(KU_UI_EVENT_KEY, 0U, static_cast<int32_t>(i)))) return 8;
    }
    if (pending(queue) != kCapacity - 1U ||
        push(queue, event(KU_UI_EVENT_POINTER, 0U, 123)) ||
        queue.dropped != 1U) return 9;
    for (unsigned i = 0; i < kCapacity - 1U; ++i) {
        if (!pop(queue, &out) || out.type != KU_UI_EVENT_KEY ||
            out.x != static_cast<int32_t>(i)) return 10;
    }
    if (pending(queue) != 0U || pop(queue, &out)) return 11;

    // When completely full, recover one truly redundant mouse position
    // even if keyboard events separate the two equivalent mouse positions.
    if (!push(queue, event(KU_UI_EVENT_POINTER, 0U, 11)) ||
        !push(queue, event(KU_UI_EVENT_KEY, 0U, 12)) ||
        !push(queue, event(KU_UI_EVENT_POINTER, 0U, 13))) return 12;
    for (unsigned i = 3; i < kCapacity - 1U; ++i) {
        if (!push(queue, event(KU_UI_EVENT_KEY, 0U, 100 + static_cast<int32_t>(i)))) return 13;
    }
    if (!push(queue, event(KU_UI_EVENT_KEY, 0U, 999)) ||
        pending(queue) != kCapacity - 1U) return 14;
    if (!pop(queue, &out) || out.type != KU_UI_EVENT_KEY || out.x != 12 ||
        !pop(queue, &out) || out.type != KU_UI_EVENT_POINTER || out.x != 13) return 15;
    for (unsigned i = 3; i < kCapacity - 1U; ++i) {
        if (!pop(queue, &out) || out.type != KU_UI_EVENT_KEY ||
            out.x != static_cast<int32_t>(100U + i)) return 16;
    }
    if (!pop(queue, &out) || out.x != 999 || pop(queue, &out)) return 17;

    // Ring indices wrap without exposing previously popped samples.
    for (unsigned i = 0; i < 256U; ++i) {
        if (!push(queue, event(KU_UI_EVENT_KEY, 0U, static_cast<int32_t>(i))) ||
            !pop(queue, &out) || out.x != static_cast<int32_t>(i)) return 18;
    }
    if (queue.dropped != 1U) return 19;
    puts("Ring-3 UI queue coalescing, edge preservation and wrap: PASS");
    return 0;
}

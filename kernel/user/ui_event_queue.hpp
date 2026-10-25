#pragma once

#include <kurogane/ui.h>

#include <stddef.h>
#include <stdint.h>

// A bounded, allocation-free queue for one Ring-3 UI context. All operations
// run in the serialized input/syscall context; this is not an MPMC primitive.
namespace user::ui_event_queue {

constexpr uint8_t kCapacity = 16U;
static_assert(kCapacity > 1U, "one slot is reserved to distinguish full/empty");

struct Queue {
    ku_ui_event events[kCapacity]{};
    uint8_t head = 0U;
    uint8_t tail = 0U;
    uint32_t dropped = 0U;
};

inline uint8_t next(uint8_t value) {
    return static_cast<uint8_t>((static_cast<unsigned>(value) + 1U) % kCapacity);
}

inline uint8_t previous(uint8_t value) {
    return value == 0U ? static_cast<uint8_t>(kCapacity - 1U)
                      : static_cast<uint8_t>(value - 1U);
}

inline size_t pending(const Queue& queue) {
    return queue.head >= queue.tail
        ? static_cast<size_t>(queue.head - queue.tail)
        : static_cast<size_t>(kCapacity + queue.head - queue.tail);
}

inline bool pop(Queue& queue, ku_ui_event* output) {
    if (output == nullptr || queue.head == queue.tail) return false;
    *output = queue.events[queue.tail];
    queue.tail = next(queue.tail);
    return true;
}

inline bool is_redundant_motion(const Queue& queue, uint8_t index) {
    const ku_ui_event& candidate = queue.events[index];
    if (candidate.type != KU_UI_EVENT_POINTER) return false;
    // A later pointer with the same pressed-button mask supersedes this
    // position. Keys and wheels between them remain untouched. Never skip
    // an intermediate button-mask transition.
    for (uint8_t pos = next(index); pos != queue.head; pos = next(pos)) {
        const ku_ui_event& later = queue.events[pos];
        if (later.type != KU_UI_EVENT_POINTER) continue;
        return later.buttons == candidate.buttons;
    }
    return false;
}

inline bool retire_redundant_motion(Queue& queue) {
    for (uint8_t index = queue.tail; index != queue.head; index = next(index)) {
        if (!is_redundant_motion(queue, index)) continue;
        // Shift the bounded ring in FIFO order. The oldest redundant move
        // disappears; key/click order and button-mask transitions survive.
        uint8_t dest = index;
        while (next(dest) != queue.head) {
            queue.events[dest] = queue.events[next(dest)];
            dest = next(dest);
        }
        queue.head = previous(queue.head);
        return true;
    }
    return false;
}

inline bool push(Queue& queue, const ku_ui_event& event) {
    if (queue.head != queue.tail) {
        ku_ui_event& last = queue.events[previous(queue.head)];
        if (event.type == KU_UI_EVENT_POINTER &&
            last.type == KU_UI_EVENT_POINTER &&
            last.buttons == event.buttons) {
            // Pointer position changed but no button edge occurred. Keep
            // only the latest coordinates rather than flooding the queue.
            last = event;
            return true;
        }
        if (event.type == KU_UI_EVENT_SCROLL &&
            last.type == KU_UI_EVENT_SCROLL &&
            ((event.wheel_y > 0 && last.wheel_y > 0) ||
             (event.wheel_y < 0 && last.wheel_y < 0))) {
            const int64_t combined = static_cast<int64_t>(last.wheel_y) +
                static_cast<int64_t>(event.wheel_y);
            last = event;
            last.wheel_y = combined > INT32_MAX ? INT32_MAX :
                combined < INT32_MIN ? INT32_MIN : static_cast<int32_t>(combined);
            return true;
        }
    }
    if (next(queue.head) == queue.tail) {
        if (!retire_redundant_motion(queue)) {
            // Saturation cannot be lossless. Reject the incoming event
            // instead of silently evicting a queued key or button edge.
            ++queue.dropped;
            return false;
        }
    }
    queue.events[queue.head] = event;
    queue.head = next(queue.head);
    return true;
}

} // namespace user::ui_event_queue

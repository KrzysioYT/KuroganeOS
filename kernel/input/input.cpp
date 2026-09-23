#include "input.hpp"

#include "../drivers/mouse.hpp"

namespace input {
namespace {

constexpr size_t QUEUE_MASK = EVENT_QUEUE_CAPACITY - 1U;
static_assert((EVENT_QUEUE_CAPACITY & QUEUE_MASK) == 0U,
              "input queue capacity must be a power of two");

Event g_events[EVENT_QUEUE_CAPACITY]{};
uint16_t g_head = 0U;
uint16_t g_tail = 0U;
uint64_t g_dropped = 0U;
int32_t g_pointer_x = 0;
int32_t g_pointer_y = 0;
int32_t g_max_x = 0;
int32_t g_max_y = 0;
uint8_t g_buttons = 0U;
bool g_initialized = false;
drivers::keyboard::KeyEvent g_pending_key{};
drivers::mouse::Sample g_pending_mouse{};
bool g_key_pending = false;
bool g_mouse_pending = false;

bool push(const Event& event) {
    if (!g_initialized) return false;
    const uint16_t head = g_head;
    if (static_cast<uint16_t>(head - g_tail) >= EVENT_QUEUE_CAPACITY) {
        ++g_dropped;
        return false;
    }
    g_events[head & QUEUE_MASK] = event;
    g_head = static_cast<uint16_t>(head + 1U);
    return true;
}

int32_t clamp(int64_t value, int32_t maximum) {
    if (value < 0) return 0;
    if (value > maximum) return maximum;
    return static_cast<int32_t>(value);
}

} // namespace

bool initialize(uint32_t screen_width, uint32_t screen_height) {
    if (screen_width == 0U || screen_height == 0U ||
        screen_width > static_cast<uint32_t>(INT32_MAX) ||
        screen_height > static_cast<uint32_t>(INT32_MAX)) return false;
    g_head = 0U;
    g_tail = 0U;
    g_dropped = 0U;
    g_max_x = static_cast<int32_t>(screen_width - 1U);
    g_max_y = static_cast<int32_t>(screen_height - 1U);
    g_pointer_x = g_max_x / 2;
    g_pointer_y = g_max_y / 2;
    g_buttons = 0U;
    g_key_pending = false;
    g_mouse_pending = false;
    g_initialized = true;
    return true;
}

size_t pump() {
    if (!g_initialized) return 0U;
    size_t processed = 0U;
    while (g_key_pending || drivers::keyboard::try_read_event(g_pending_key)) {
        g_key_pending = true;
        if (!submit_key(g_pending_key)) return processed;
        g_key_pending = false;
        ++processed;
    }
    while (g_mouse_pending || drivers::mouse::try_read_sample(&g_pending_mouse)) {
        g_mouse_pending = true;
        if (!submit_mouse(g_pending_mouse)) return processed;
        g_mouse_pending = false;
        ++processed;
    }
    return processed;
}

bool submit_key(const drivers::keyboard::KeyEvent& event) {
    return push({
        event.pressed ? EventType::KeyDown : EventType::KeyUp,
        event.key,
        event.character,
        g_pointer_x,
        g_pointer_y,
        0,
        0,
        0,
        0U,
        g_buttons,
        event.shift,
        event.control,
        event.alt
    });
}

bool submit_mouse(const drivers::mouse::Sample& sample) {
    if (!g_initialized) return false;
    const int32_t next_x = clamp(
        static_cast<int64_t>(g_pointer_x) + sample.delta_x, g_max_x);
    const int32_t next_y = clamp(
        static_cast<int64_t>(g_pointer_y) + sample.delta_y, g_max_y);
    // One report can produce motion, three button changes and a wheel event.
    // Stage the whole batch so a caller may retry an unaccepted report without
    // duplicating motion or losing the release half of a button transition.
    Event events[5]{};
    size_t count = 0U;
    if (next_x != g_pointer_x || next_y != g_pointer_y) {
        events[count++] = {
            EventType::MouseMove, drivers::keyboard::KeyCode::Unknown, 0,
            next_x, next_y,
            sample.delta_x, sample.delta_y, 0, 0U,
            sample.buttons, false, false, false
        };
    }
    constexpr uint8_t buttons[] = {
        drivers::mouse::Left,
        drivers::mouse::Right,
        drivers::mouse::Middle
    };
    for (uint8_t button : buttons) {
        if ((sample.changed_buttons & button) == 0U) continue;
        events[count++] = {
            (sample.buttons & button) != 0U
                ? EventType::MouseButtonDown
                : EventType::MouseButtonUp,
            drivers::keyboard::KeyCode::Unknown, 0,
            next_x, next_y, 0, 0, 0, button,
            sample.buttons, false, false, false
        };
    }
    if (sample.wheel != 0) {
        events[count++] = {
            EventType::MouseWheel, drivers::keyboard::KeyCode::Unknown, 0,
            next_x, next_y, 0, 0, sample.wheel, 0U,
            sample.buttons, false, false, false
        };
    }
    const size_t occupied = static_cast<uint16_t>(g_head - g_tail);
    if (count > EVENT_QUEUE_CAPACITY - occupied) {
        g_dropped += count;
        return false;
    }
    // Producers/consumer are serialized by the existing input polling owner.
    // Publish the head only after every event and its pointer state is ready.
    for (size_t index = 0U; index < count; ++index) {
        g_events[(g_head + index) & QUEUE_MASK] = events[index];
    }
    g_pointer_x = next_x;
    g_pointer_y = next_y;
    g_buttons = sample.buttons;
    g_head = static_cast<uint16_t>(g_head + count);
    return true;
}

bool try_read(Event* out_event) {
    if (!g_initialized || out_event == nullptr || g_tail == g_head) return false;
    *out_event = g_events[g_tail & QUEUE_MASK];
    g_tail = static_cast<uint16_t>(g_tail + 1U);
    return true;
}

size_t pending_events() {
    return g_initialized
        ? static_cast<size_t>(static_cast<uint16_t>(g_head - g_tail))
        : 0U;
}

uint64_t dropped_events() { return g_dropped; }
int32_t pointer_x() { return g_pointer_x; }
int32_t pointer_y() { return g_pointer_y; }
uint8_t pointer_buttons() { return g_buttons; }

} // namespace input

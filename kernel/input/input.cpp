#include "input.hpp"

#ifndef KUROGANE_HOST_TEST
#include "../drivers/mouse.hpp"
#endif

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
    g_initialized = true;
    return true;
}

size_t pump() {
#ifdef KUROGANE_HOST_TEST
    return 0U;
#else
    size_t processed = 0U;
    drivers::keyboard::KeyEvent key{};
    while (drivers::keyboard::try_read_event(key)) {
        static_cast<void>(submit_key(key));
        ++processed;
    }
    drivers::mouse::Sample mouse{};
    while (drivers::mouse::try_read_sample(&mouse)) {
        static_cast<void>(submit_mouse(mouse));
        ++processed;
    }
    return processed;
#endif
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
    const int32_t old_x = g_pointer_x;
    const int32_t old_y = g_pointer_y;
    g_pointer_x = clamp(
        static_cast<int64_t>(g_pointer_x) + sample.delta_x, g_max_x);
    g_pointer_y = clamp(
        static_cast<int64_t>(g_pointer_y) + sample.delta_y, g_max_y);
    bool accepted = true;
    if (g_pointer_x != old_x || g_pointer_y != old_y) {
        accepted = push({
            EventType::MouseMove, drivers::keyboard::KeyCode::Unknown, 0,
            g_pointer_x, g_pointer_y,
            sample.delta_x, sample.delta_y, 0, 0U,
            sample.buttons, false, false, false
        }) && accepted;
    }
    constexpr uint8_t buttons[] = {
        drivers::mouse::Left,
        drivers::mouse::Right,
        drivers::mouse::Middle
    };
    for (uint8_t button : buttons) {
        if ((sample.changed_buttons & button) == 0U) continue;
        accepted = push({
            (sample.buttons & button) != 0U
                ? EventType::MouseButtonDown
                : EventType::MouseButtonUp,
            drivers::keyboard::KeyCode::Unknown, 0,
            g_pointer_x, g_pointer_y, 0, 0, 0, button,
            sample.buttons, false, false, false
        }) && accepted;
    }
    if (sample.wheel != 0) {
        accepted = push({
            EventType::MouseWheel, drivers::keyboard::KeyCode::Unknown, 0,
            g_pointer_x, g_pointer_y, 0, 0, sample.wheel, 0U,
            sample.buttons, false, false, false
        }) && accepted;
    }
    g_buttons = sample.buttons;
    return accepted;
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

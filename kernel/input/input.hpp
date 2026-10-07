#pragma once

#include "../drivers/keyboard.hpp"
#include "../drivers/mouse_protocol.hpp"

#include <stddef.h>
#include <stdint.h>

namespace input {

enum class EventType : uint8_t {
    KeyDown = 0,
    KeyUp,
    MouseMove,
    MouseButtonDown,
    MouseButtonUp,
    MouseWheel,
};

struct AbsolutePointerSample {
    int32_t x;
    int32_t y;
    int32_t logical_minimum_x;
    int32_t logical_maximum_x;
    int32_t logical_minimum_y;
    int32_t logical_maximum_y;
    int8_t wheel;
    uint8_t buttons;
    uint8_t changed_buttons;
};

struct Event {
    EventType type;
    drivers::keyboard::KeyCode key;
    char character;
    int32_t x;
    int32_t y;
    int16_t delta_x;
    int16_t delta_y;
    int8_t wheel;
    uint8_t button;
    uint8_t buttons;
    bool shift;
    bool control;
    bool alt;
};

constexpr size_t EVENT_QUEUE_CAPACITY = 256U;

bool initialize(uint32_t screen_width, uint32_t screen_height);
size_t pump();
bool submit_key(const drivers::keyboard::KeyEvent& event);
// Serialized polling context. On false, queue and pointer/button state are
// unchanged; callers retaining the sample can retry without duplicate events.
bool submit_mouse(const drivers::mouse::Sample& sample);
// Transport-neutral absolute pointer path for USB tablets and future
// touch/pen backends. Logical device coordinates are scaled to the current
// framebuffer bounds before the event batch is published atomically.
bool submit_absolute_pointer(const AbsolutePointerSample& sample);
bool try_read(Event* out_event);
size_t pending_events();
uint64_t dropped_events();
int32_t pointer_x();
int32_t pointer_y();
uint8_t pointer_buttons();

} // namespace input

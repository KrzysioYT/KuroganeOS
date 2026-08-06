#pragma once

#include <stddef.h>
#include <stdint.h>

namespace drivers::keyboard {

enum class KeyCode : uint16_t {
    Unknown = 0,
    Escape,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    Digit0,
    Minus,
    Equals,
    Backspace,
    Tab,
    Q,
    W,
    E,
    R,
    T,
    Y,
    U,
    I,
    O,
    P,
    LeftBracket,
    RightBracket,
    Enter,
    LeftControl,
    A,
    S,
    D,
    F,
    G,
    H,
    J,
    K,
    L,
    Semicolon,
    Apostrophe,
    Grave,
    LeftShift,
    Backslash,
    Z,
    X,
    C,
    V,
    B,
    N,
    M,
    Comma,
    Period,
    Slash,
    RightShift,
    KeypadMultiply,
    LeftAlt,
    Space,
    CapsLock,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    NumLock,
    ScrollLock,
    F11,
    F12,
    KeypadEnter,
    RightControl,
    KeypadDivide,
    RightAlt,
    Home,
    ArrowUp,
    PageUp,
    ArrowLeft,
    ArrowRight,
    End,
    ArrowDown,
    PageDown,
    Insert,
    Delete,
    LeftGui,
    RightGui,
    Menu
};

struct KeyEvent {
    KeyCode key;
    char character;
    uint8_t scancode;
    bool pressed;
    bool extended;
    bool shift;
    bool caps_lock;
    bool control;
    bool alt;
};

constexpr size_t EVENT_BUFFER_CAPACITY = 128;

// Installs IRQ1 and attempts a bounded PS/2 controller configuration. A false
// hardware configuration result does not disable the polling decoder.
bool initialize();
void shutdown();
bool initialized();
bool controller_configured();

// IRQ entry and polling fallback. Both drain every currently queued keyboard
// byte; the central IRQ dispatcher sends EOI.
void handle_irq();
size_t poll();

// Public decoder entry is useful for early boot and hardware-independent tests.
void process_scancode(uint8_t scancode);

bool try_read_event(KeyEvent& event);
bool try_read_char(char& character);
size_t pending_events();
uint64_t dropped_events();
void clear_events();

bool shift_active();
bool caps_lock_active();
bool control_active();
bool alt_active();

} // namespace drivers::keyboard

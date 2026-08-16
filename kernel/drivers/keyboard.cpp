#include "keyboard.hpp"

#include "../arch/x86_64/interrupts.hpp"
#include "pic.hpp"
#include "mouse.hpp"

namespace drivers::keyboard {

namespace {

constexpr uint16_t DATA_PORT = 0x60;
constexpr uint16_t STATUS_COMMAND_PORT = 0x64;

constexpr uint8_t STATUS_OUTPUT_FULL = 1u << 0;
constexpr uint8_t STATUS_INPUT_FULL = 1u << 1;
constexpr uint8_t STATUS_AUXILIARY_DATA = 1u << 5;

constexpr uint8_t COMMAND_DISABLE_FIRST_PORT = 0xAD;
constexpr uint8_t COMMAND_ENABLE_FIRST_PORT = 0xAE;
constexpr uint8_t COMMAND_READ_CONFIG = 0x20;
constexpr uint8_t COMMAND_WRITE_CONFIG = 0x60;
constexpr uint8_t KEYBOARD_ENABLE_SCANNING = 0xF4;
constexpr uint8_t KEYBOARD_ACK = 0xFA;
constexpr uint8_t KEYBOARD_RESEND = 0xFE;

constexpr size_t IO_TIMEOUT = 100000;
constexpr size_t MAX_DRAIN_BYTES = 32;
constexpr size_t EVENT_BUFFER_MASK = EVENT_BUFFER_CAPACITY - 1;

static_assert(
    (EVENT_BUFFER_CAPACITY & (EVENT_BUFFER_CAPACITY - 1)) == 0,
    "keyboard ring-buffer capacity must be a power of two");

static KeyEvent g_events[EVENT_BUFFER_CAPACITY];
static uint16_t g_head = 0;
static uint16_t g_tail = 0;
alignas(8) static uint64_t g_dropped_events = 0;

static bool g_initialized = false;
static bool g_controller_configured = false;
static volatile bool g_left_shift = false;
static volatile bool g_right_shift = false;
static volatile bool g_left_control = false;
static volatile bool g_right_control = false;
static volatile bool g_left_alt = false;
static volatile bool g_right_alt = false;
static volatile bool g_caps_lock = false;
static volatile bool g_caps_key_down = false;
static bool g_extended_prefix = false;
static uint8_t g_pause_bytes_remaining = 0;

inline void out8(uint16_t port, uint8_t value) {
    __asm__ volatile(
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
        : "memory");
}

inline uint8_t in8(uint16_t port) {
    uint8_t value = 0;
    __asm__ volatile(
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
        : "memory");
    return value;
}

inline void io_wait() {
    out8(0x80, 0);
}

bool wait_input_empty() {
    for (size_t i = 0; i < IO_TIMEOUT; ++i) {
        if ((in8(STATUS_COMMAND_PORT) & STATUS_INPUT_FULL) == 0) {
            return true;
        }
        io_wait();
    }
    return false;
}

bool wait_output_full() {
    for (size_t i = 0; i < IO_TIMEOUT; ++i) {
        if ((in8(STATUS_COMMAND_PORT) & STATUS_OUTPUT_FULL) != 0) {
            return true;
        }
        io_wait();
    }
    return false;
}

bool write_controller_command(uint8_t command) {
    if (!wait_input_empty()) {
        return false;
    }
    out8(STATUS_COMMAND_PORT, command);
    return true;
}

bool write_data(uint8_t value) {
    if (!wait_input_empty()) {
        return false;
    }
    out8(DATA_PORT, value);
    return true;
}

bool read_data(uint8_t& value) {
    if (!wait_output_full()) {
        return false;
    }
    value = in8(DATA_PORT);
    return true;
}

void flush_output() {
    for (size_t i = 0; i < MAX_DRAIN_BYTES; ++i) {
        if ((in8(STATUS_COMMAND_PORT) & STATUS_OUTPUT_FULL) == 0) {
            return;
        }
        static_cast<void>(in8(DATA_PORT));
    }
}

bool send_keyboard_command(uint8_t command) {
    for (size_t attempt = 0; attempt < 3; ++attempt) {
        if (!write_data(command)) {
            return false;
        }

        uint8_t response = 0;
        if (!read_data(response)) {
            return false;
        }
        if (response == KEYBOARD_ACK) {
            return true;
        }
        if (response != KEYBOARD_RESEND) {
            return false;
        }
    }
    return false;
}

bool configure_controller() {
    if (!write_controller_command(COMMAND_DISABLE_FIRST_PORT)) {
        return false;
    }
    flush_output();

    uint8_t config = 0;
    bool config_written = false;
    if (write_controller_command(COMMAND_READ_CONFIG) &&
        read_data(config)) {
        // Enable IRQ1 and the first clock. Translation exposes scan-code set 1
        // even when the keyboard itself powers up in scan-code set 2.
        config = static_cast<uint8_t>(
            (config | (1u << 0) | (1u << 6)) & ~(1u << 4));
        config_written =
            write_controller_command(COMMAND_WRITE_CONFIG) &&
            write_data(config);
    }

    // Always attempt to re-enable the port after disabling it, including
    // controller read/write failures.
    const bool port_enabled =
        write_controller_command(COMMAND_ENABLE_FIRST_PORT);
    if (!config_written || !port_enabled) {
        return false;
    }

    flush_output();
    return send_keyboard_command(KEYBOARD_ENABLE_SCANNING);
}

void reset_decoder_state() {
    __atomic_store_n(&g_head, static_cast<uint16_t>(0), __ATOMIC_RELAXED);
    __atomic_store_n(&g_tail, static_cast<uint16_t>(0), __ATOMIC_RELAXED);
    __atomic_store_n(
        &g_dropped_events,
        static_cast<uint64_t>(0),
        __ATOMIC_RELAXED);
    g_left_shift = false;
    g_right_shift = false;
    g_left_control = false;
    g_right_control = false;
    g_left_alt = false;
    g_right_alt = false;
    g_caps_lock = false;
    g_caps_key_down = false;
    g_extended_prefix = false;
    g_pause_bytes_remaining = 0;
}

KeyCode translate_key(uint8_t scancode, bool extended) {
    if (extended) {
        switch (scancode) {
            case 0x1C: return KeyCode::KeypadEnter;
            case 0x1D: return KeyCode::RightControl;
            case 0x35: return KeyCode::KeypadDivide;
            case 0x38: return KeyCode::RightAlt;
            case 0x47: return KeyCode::Home;
            case 0x48: return KeyCode::ArrowUp;
            case 0x49: return KeyCode::PageUp;
            case 0x4B: return KeyCode::ArrowLeft;
            case 0x4D: return KeyCode::ArrowRight;
            case 0x4F: return KeyCode::End;
            case 0x50: return KeyCode::ArrowDown;
            case 0x51: return KeyCode::PageDown;
            case 0x52: return KeyCode::Insert;
            case 0x53: return KeyCode::Delete;
            case 0x5B: return KeyCode::LeftGui;
            case 0x5C: return KeyCode::RightGui;
            case 0x5D: return KeyCode::Menu;
            default: return KeyCode::Unknown;
        }
    }

    switch (scancode) {
        case 0x01: return KeyCode::Escape;
        case 0x02: return KeyCode::Digit1;
        case 0x03: return KeyCode::Digit2;
        case 0x04: return KeyCode::Digit3;
        case 0x05: return KeyCode::Digit4;
        case 0x06: return KeyCode::Digit5;
        case 0x07: return KeyCode::Digit6;
        case 0x08: return KeyCode::Digit7;
        case 0x09: return KeyCode::Digit8;
        case 0x0A: return KeyCode::Digit9;
        case 0x0B: return KeyCode::Digit0;
        case 0x0C: return KeyCode::Minus;
        case 0x0D: return KeyCode::Equals;
        case 0x0E: return KeyCode::Backspace;
        case 0x0F: return KeyCode::Tab;
        case 0x10: return KeyCode::Q;
        case 0x11: return KeyCode::W;
        case 0x12: return KeyCode::E;
        case 0x13: return KeyCode::R;
        case 0x14: return KeyCode::T;
        case 0x15: return KeyCode::Y;
        case 0x16: return KeyCode::U;
        case 0x17: return KeyCode::I;
        case 0x18: return KeyCode::O;
        case 0x19: return KeyCode::P;
        case 0x1A: return KeyCode::LeftBracket;
        case 0x1B: return KeyCode::RightBracket;
        case 0x1C: return KeyCode::Enter;
        case 0x1D: return KeyCode::LeftControl;
        case 0x1E: return KeyCode::A;
        case 0x1F: return KeyCode::S;
        case 0x20: return KeyCode::D;
        case 0x21: return KeyCode::F;
        case 0x22: return KeyCode::G;
        case 0x23: return KeyCode::H;
        case 0x24: return KeyCode::J;
        case 0x25: return KeyCode::K;
        case 0x26: return KeyCode::L;
        case 0x27: return KeyCode::Semicolon;
        case 0x28: return KeyCode::Apostrophe;
        case 0x29: return KeyCode::Grave;
        case 0x2A: return KeyCode::LeftShift;
        case 0x2B: return KeyCode::Backslash;
        case 0x2C: return KeyCode::Z;
        case 0x2D: return KeyCode::X;
        case 0x2E: return KeyCode::C;
        case 0x2F: return KeyCode::V;
        case 0x30: return KeyCode::B;
        case 0x31: return KeyCode::N;
        case 0x32: return KeyCode::M;
        case 0x33: return KeyCode::Comma;
        case 0x34: return KeyCode::Period;
        case 0x35: return KeyCode::Slash;
        case 0x36: return KeyCode::RightShift;
        case 0x37: return KeyCode::KeypadMultiply;
        case 0x38: return KeyCode::LeftAlt;
        case 0x39: return KeyCode::Space;
        case 0x3A: return KeyCode::CapsLock;
        case 0x3B: return KeyCode::F1;
        case 0x3C: return KeyCode::F2;
        case 0x3D: return KeyCode::F3;
        case 0x3E: return KeyCode::F4;
        case 0x3F: return KeyCode::F5;
        case 0x40: return KeyCode::F6;
        case 0x41: return KeyCode::F7;
        case 0x42: return KeyCode::F8;
        case 0x43: return KeyCode::F9;
        case 0x44: return KeyCode::F10;
        case 0x45: return KeyCode::NumLock;
        case 0x46: return KeyCode::ScrollLock;
        case 0x57: return KeyCode::F11;
        case 0x58: return KeyCode::F12;
        default: return KeyCode::Unknown;
    }
}

char letter_for_key(KeyCode key) {
    switch (key) {
        case KeyCode::A: return 'a';
        case KeyCode::B: return 'b';
        case KeyCode::C: return 'c';
        case KeyCode::D: return 'd';
        case KeyCode::E: return 'e';
        case KeyCode::F: return 'f';
        case KeyCode::G: return 'g';
        case KeyCode::H: return 'h';
        case KeyCode::I: return 'i';
        case KeyCode::J: return 'j';
        case KeyCode::K: return 'k';
        case KeyCode::L: return 'l';
        case KeyCode::M: return 'm';
        case KeyCode::N: return 'n';
        case KeyCode::O: return 'o';
        case KeyCode::P: return 'p';
        case KeyCode::Q: return 'q';
        case KeyCode::R: return 'r';
        case KeyCode::S: return 's';
        case KeyCode::T: return 't';
        case KeyCode::U: return 'u';
        case KeyCode::V: return 'v';
        case KeyCode::W: return 'w';
        case KeyCode::X: return 'x';
        case KeyCode::Y: return 'y';
        case KeyCode::Z: return 'z';
        default: return 0;
    }
}

char translate_character(KeyCode key, bool shift, bool caps, bool control) {
    char letter = letter_for_key(key);
    if (letter != 0) {
        if (control) {
            return static_cast<char>(letter - 'a' + 1);
        }
        if (shift != caps) {
            return static_cast<char>(letter - 'a' + 'A');
        }
        return letter;
    }

    switch (key) {
        case KeyCode::Escape: return '\x1B';
        case KeyCode::Backspace: return '\b';
        case KeyCode::Tab: return '\t';
        case KeyCode::Enter:
        case KeyCode::KeypadEnter: return '\n';
        case KeyCode::Space: return ' ';
        case KeyCode::Digit1: return shift ? '!' : '1';
        case KeyCode::Digit2: return shift ? '@' : '2';
        case KeyCode::Digit3: return shift ? '#' : '3';
        case KeyCode::Digit4: return shift ? '$' : '4';
        case KeyCode::Digit5: return shift ? '%' : '5';
        case KeyCode::Digit6: return shift ? '^' : '6';
        case KeyCode::Digit7: return shift ? '&' : '7';
        case KeyCode::Digit8: return shift ? '*' : '8';
        case KeyCode::Digit9: return shift ? '(' : '9';
        case KeyCode::Digit0: return shift ? ')' : '0';
        case KeyCode::Minus: return shift ? '_' : '-';
        case KeyCode::Equals: return shift ? '+' : '=';
        case KeyCode::LeftBracket: return shift ? '{' : '[';
        case KeyCode::RightBracket: return shift ? '}' : ']';
        case KeyCode::Semicolon: return shift ? ':' : ';';
        case KeyCode::Apostrophe: return shift ? '"' : '\'';
        case KeyCode::Grave: return shift ? '~' : '`';
        case KeyCode::Backslash: return shift ? '|' : '\\';
        case KeyCode::Comma: return shift ? '<' : ',';
        case KeyCode::Period: return shift ? '>' : '.';
        case KeyCode::Slash:
        case KeyCode::KeypadDivide: return shift ? '?' : '/';
        case KeyCode::KeypadMultiply: return '*';
        default: return 0;
    }
}

void push_event(const KeyEvent& event) {
    const uint16_t head =
        __atomic_load_n(&g_head, __ATOMIC_RELAXED);
    const uint16_t tail =
        __atomic_load_n(&g_tail, __ATOMIC_ACQUIRE);

    if (static_cast<uint16_t>(head - tail) >=
        EVENT_BUFFER_CAPACITY) {
        __atomic_fetch_add(
            &g_dropped_events,
            static_cast<uint64_t>(1),
            __ATOMIC_RELAXED);
        return;
    }

    g_events[head & EVENT_BUFFER_MASK] = event;
    const uint16_t next = static_cast<uint16_t>(head + 1);
    __atomic_store_n(&g_head, next, __ATOMIC_RELEASE);
}

size_t drain_controller() {
    size_t processed = 0;
    for (size_t i = 0; i < MAX_DRAIN_BYTES; ++i) {
        const uint8_t status = in8(STATUS_COMMAND_PORT);
        if ((status & STATUS_OUTPUT_FULL) == 0) {
            break;
        }

        const uint8_t data = in8(DATA_PORT);
        if ((status & STATUS_AUXILIARY_DATA) == 0) {
            process_scancode(data);
            ++processed;
        } else {
            mouse::process_byte(data);
        }
    }
    return processed;
}

} // namespace

bool initialize() {
    drivers::pic::mask(1);
    reset_decoder_state();

    if (!arch::x86_64::interrupts::register_irq_handler(
            1,
            handle_irq)) {
        g_initialized = false;
        g_controller_configured = false;
        return false;
    }

    g_controller_configured = configure_controller();
    g_initialized = drivers::pic::unmask(1);
    if (!g_initialized) {
        arch::x86_64::interrupts::unregister_irq_handler(1);
    }
    // Keep polling available when an odd controller only partially responds,
    // but do not advertise the keyboard as fully ready in that state.
    return g_initialized && g_controller_configured;
}

void shutdown() {
    drivers::pic::mask(1);
    arch::x86_64::interrupts::unregister_irq_handler(1);
    g_initialized = false;
    g_controller_configured = false;
}

bool initialized() {
    return g_initialized;
}

bool controller_configured() {
    return g_controller_configured;
}

void handle_irq() {
    drain_controller();
}

size_t poll() {
    const bool restore_interrupts =
        arch::x86_64::interrupts::enabled();
    arch::x86_64::interrupts::disable();
    const size_t processed = drain_controller();
    if (restore_interrupts) {
        arch::x86_64::interrupts::enable();
    }
    return processed;
}

void process_scancode(uint8_t raw_scancode) {
    if (g_pause_bytes_remaining != 0) {
        --g_pause_bytes_remaining;
        return;
    }

    if (raw_scancode == 0xE1) {
        g_pause_bytes_remaining = 5;
        g_extended_prefix = false;
        return;
    }
    if (raw_scancode == 0xE0) {
        g_extended_prefix = true;
        return;
    }

    const bool extended = g_extended_prefix;
    g_extended_prefix = false;
    const bool pressed = (raw_scancode & 0x80) == 0;
    const uint8_t scancode =
        static_cast<uint8_t>(raw_scancode & 0x7F);
    const KeyCode key = translate_key(scancode, extended);

    if (!extended && key == KeyCode::LeftShift) {
        g_left_shift = pressed;
    } else if (!extended && key == KeyCode::RightShift) {
        g_right_shift = pressed;
    } else if (key == KeyCode::LeftControl) {
        g_left_control = pressed;
    } else if (key == KeyCode::RightControl) {
        g_right_control = pressed;
    } else if (key == KeyCode::LeftAlt) {
        g_left_alt = pressed;
    } else if (key == KeyCode::RightAlt) {
        g_right_alt = pressed;
    } else if (!extended && key == KeyCode::CapsLock) {
        if (pressed && !g_caps_key_down) {
            g_caps_lock = !g_caps_lock;
        }
        g_caps_key_down = pressed;
    }

    const bool shift = g_left_shift || g_right_shift;
    const bool control = g_left_control || g_right_control;
    const bool alt = g_left_alt || g_right_alt;
    const KeyEvent event = {
        key,
        translate_character(key, shift, g_caps_lock, control),
        scancode,
        pressed,
        extended,
        shift,
        g_caps_lock,
        control,
        alt
    };
    push_event(event);
}

bool try_read_event(KeyEvent& event) {
    uint16_t tail =
        __atomic_load_n(&g_tail, __ATOMIC_RELAXED);
    uint16_t head =
        __atomic_load_n(&g_head, __ATOMIC_ACQUIRE);
    if (tail == head) {
        poll();
        tail = __atomic_load_n(&g_tail, __ATOMIC_RELAXED);
        head = __atomic_load_n(&g_head, __ATOMIC_ACQUIRE);
        if (tail == head) {
            return false;
        }
    }

    event = g_events[tail & EVENT_BUFFER_MASK];
    const uint16_t next = static_cast<uint16_t>(tail + 1);
    __atomic_store_n(&g_tail, next, __ATOMIC_RELEASE);
    return true;
}

bool try_read_char(char& character) {
    KeyEvent event = {};
    while (try_read_event(event)) {
        if (event.pressed && event.character != 0) {
            character = event.character;
            return true;
        }
    }
    return false;
}

size_t pending_events() {
    const uint16_t head =
        __atomic_load_n(&g_head, __ATOMIC_ACQUIRE);
    const uint16_t tail =
        __atomic_load_n(&g_tail, __ATOMIC_ACQUIRE);
    return static_cast<size_t>(
        static_cast<uint16_t>(head - tail));
}

uint64_t dropped_events() {
    return __atomic_load_n(&g_dropped_events, __ATOMIC_RELAXED);
}

void clear_events() {
    const bool restore_interrupts =
        arch::x86_64::interrupts::enabled();
    arch::x86_64::interrupts::disable();
    const uint16_t head =
        __atomic_load_n(&g_head, __ATOMIC_RELAXED);
    __atomic_store_n(&g_tail, head, __ATOMIC_RELEASE);
    if (restore_interrupts) {
        arch::x86_64::interrupts::enable();
    }
}

bool shift_active() {
    return g_left_shift || g_right_shift;
}

bool caps_lock_active() {
    return g_caps_lock;
}

bool control_active() {
    return g_left_control || g_right_control;
}

bool alt_active() {
    return g_left_alt || g_right_alt;
}

} // namespace drivers::keyboard

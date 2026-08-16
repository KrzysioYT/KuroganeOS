#include "protocol.hpp"

namespace drivers::usb {
namespace {

bool contains(const uint8_t* keys, uint8_t usage) {
    for (size_t index = 0U; index < 6U; ++index) {
        if (keys[index] == usage) return true;
    }
    return false;
}

keyboard::KeyCode usage_key(uint8_t usage) {
    using keyboard::KeyCode;
    static constexpr KeyCode letters[] = {
        KeyCode::A, KeyCode::B, KeyCode::C, KeyCode::D, KeyCode::E,
        KeyCode::F, KeyCode::G, KeyCode::H, KeyCode::I, KeyCode::J,
        KeyCode::K, KeyCode::L, KeyCode::M, KeyCode::N, KeyCode::O,
        KeyCode::P, KeyCode::Q, KeyCode::R, KeyCode::S, KeyCode::T,
        KeyCode::U, KeyCode::V, KeyCode::W, KeyCode::X, KeyCode::Y,
        KeyCode::Z,
    };
    static constexpr KeyCode digits[] = {
        KeyCode::Digit1, KeyCode::Digit2, KeyCode::Digit3,
        KeyCode::Digit4, KeyCode::Digit5, KeyCode::Digit6,
        KeyCode::Digit7, KeyCode::Digit8, KeyCode::Digit9,
        KeyCode::Digit0,
    };
    if (usage >= 0x04U && usage <= 0x1DU) return letters[usage - 0x04U];
    if (usage >= 0x1EU && usage <= 0x27U) return digits[usage - 0x1EU];
    switch (usage) {
        case 0x28U: return KeyCode::Enter;
        case 0x29U: return KeyCode::Escape;
        case 0x2AU: return KeyCode::Backspace;
        case 0x2BU: return KeyCode::Tab;
        case 0x2CU: return KeyCode::Space;
        case 0x2DU: return KeyCode::Minus;
        case 0x2EU: return KeyCode::Equals;
        case 0x2FU: return KeyCode::LeftBracket;
        case 0x30U: return KeyCode::RightBracket;
        case 0x31U: return KeyCode::Backslash;
        case 0x33U: return KeyCode::Semicolon;
        case 0x34U: return KeyCode::Apostrophe;
        case 0x35U: return KeyCode::Grave;
        case 0x36U: return KeyCode::Comma;
        case 0x37U: return KeyCode::Period;
        case 0x38U: return KeyCode::Slash;
        case 0x39U: return KeyCode::CapsLock;
        case 0x3AU: return KeyCode::F1;
        case 0x3BU: return KeyCode::F2;
        case 0x3CU: return KeyCode::F3;
        case 0x3DU: return KeyCode::F4;
        case 0x3EU: return KeyCode::F5;
        case 0x3FU: return KeyCode::F6;
        case 0x40U: return KeyCode::F7;
        case 0x41U: return KeyCode::F8;
        case 0x42U: return KeyCode::F9;
        case 0x43U: return KeyCode::F10;
        case 0x44U: return KeyCode::F11;
        case 0x45U: return KeyCode::F12;
        case 0x47U: return KeyCode::ScrollLock;
        case 0x49U: return KeyCode::Insert;
        case 0x4AU: return KeyCode::Home;
        case 0x4BU: return KeyCode::PageUp;
        case 0x4CU: return KeyCode::Delete;
        case 0x4DU: return KeyCode::End;
        case 0x4EU: return KeyCode::PageDown;
        case 0x4FU: return KeyCode::ArrowRight;
        case 0x50U: return KeyCode::ArrowLeft;
        case 0x51U: return KeyCode::ArrowDown;
        case 0x52U: return KeyCode::ArrowUp;
        case 0x53U: return KeyCode::NumLock;
        case 0x54U: return KeyCode::KeypadDivide;
        case 0x55U: return KeyCode::KeypadMultiply;
        case 0x58U: return KeyCode::KeypadEnter;
        case 0xE0U: return KeyCode::LeftControl;
        case 0xE1U: return KeyCode::LeftShift;
        case 0xE2U: return KeyCode::LeftAlt;
        case 0xE3U: return KeyCode::LeftGui;
        case 0xE4U: return KeyCode::RightControl;
        case 0xE5U: return KeyCode::RightShift;
        case 0xE6U: return KeyCode::RightAlt;
        case 0xE7U: return KeyCode::RightGui;
        default: return KeyCode::Unknown;
    }
}

char usage_character(uint8_t usage, bool shift, bool caps_lock) {
    if (usage >= 0x04U && usage <= 0x1DU) {
        const char lower = static_cast<char>('a' + usage - 0x04U);
        return shift != caps_lock
            ? static_cast<char>(lower - 'a' + 'A')
            : lower;
    }
    static constexpr char plain_digits[] = "1234567890";
    static constexpr char shift_digits[] = "!@#$%^&*()";
    if (usage >= 0x1EU && usage <= 0x27U) {
        return shift ? shift_digits[usage - 0x1EU]
                     : plain_digits[usage - 0x1EU];
    }
    switch (usage) {
        case 0x28U: return '\n';
        case 0x2AU: return '\b';
        case 0x2BU: return '\t';
        case 0x2CU: return ' ';
        case 0x2DU: return shift ? '_' : '-';
        case 0x2EU: return shift ? '+' : '=';
        case 0x2FU: return shift ? '{' : '[';
        case 0x30U: return shift ? '}' : ']';
        case 0x31U: return shift ? '|' : '\\';
        case 0x33U: return shift ? ':' : ';';
        case 0x34U: return shift ? '"' : '\'';
        case 0x35U: return shift ? '~' : '`';
        case 0x36U: return shift ? '<' : ',';
        case 0x37U: return shift ? '>' : '.';
        case 0x38U: return shift ? '?' : '/';
        default: return 0;
    }
}

bool append_event(
    keyboard::KeyEvent* events,
    size_t capacity,
    size_t* count,
    uint8_t usage,
    bool pressed,
    uint8_t modifiers,
    bool caps_lock) {
    const keyboard::KeyCode key = usage_key(usage);
    if (key == keyboard::KeyCode::Unknown) return true;
    if (*count >= capacity) return false;
    const bool shift = (modifiers & 0x22U) != 0U;
    events[(*count)++] = {
        key,
        pressed ? usage_character(usage, shift, caps_lock) : static_cast<char>(0),
        usage,
        pressed,
        usage >= 0xE4U,
        shift,
        caps_lock,
        (modifiers & 0x11U) != 0U,
        (modifiers & 0x44U) != 0U,
    };
    return true;
}

} // namespace

bool find_boot_keyboard_interface(
    const uint8_t* descriptors,
    size_t length,
    HidBootKeyboardInterface* output) {
    if (descriptors == nullptr || output == nullptr || length < 9U ||
        descriptors[1U] != 2U || descriptors[0U] < 9U) {
        return false;
    }
    const uint16_t total = static_cast<uint16_t>(descriptors[2U]) |
        static_cast<uint16_t>(descriptors[3U]) << 8U;
    if (total < 9U || total > length || descriptors[5U] == 0U) return false;
    const uint8_t configuration = descriptors[5U];
    bool keyboard_interface = false;
    uint8_t interface_number = 0U;
    for (size_t offset = 0U; offset < total;) {
        if (total - offset < 2U) return false;
        const uint8_t descriptor_length = descriptors[offset];
        const uint8_t descriptor_type = descriptors[offset + 1U];
        if (descriptor_length < 2U || descriptor_length > total - offset) {
            return false;
        }
        if (descriptor_type == 4U) {
            if (descriptor_length < 9U) return false;
            keyboard_interface = descriptors[offset + 3U] == 0U &&
                descriptors[offset + 5U] == 3U &&
                descriptors[offset + 6U] == 1U &&
                descriptors[offset + 7U] == 1U;
            interface_number = descriptors[offset + 2U];
        } else if (descriptor_type == 5U && keyboard_interface) {
            if (descriptor_length < 7U) return false;
            const uint8_t endpoint = descriptors[offset + 2U];
            const uint8_t attributes = descriptors[offset + 3U];
            const uint16_t packet =
                static_cast<uint16_t>(descriptors[offset + 4U]) |
                static_cast<uint16_t>(descriptors[offset + 5U]) << 8U;
            if ((endpoint & 0x80U) != 0U && (attributes & 3U) == 3U &&
                (packet & 0x7FFU) >= 8U) {
                *output = {
                    configuration,
                    interface_number,
                    endpoint,
                    static_cast<uint16_t>(packet & 0x7FFU),
                    descriptors[offset + 6U],
                };
                return true;
            }
        }
        offset += descriptor_length;
    }
    return false;
}

void reset_keyboard_decoder(KeyboardDecoder* decoder) {
    if (decoder != nullptr) *decoder = {};
}

bool decode_boot_keyboard_report(
    KeyboardDecoder* decoder,
    const uint8_t* report,
    size_t report_length,
    keyboard::KeyEvent* events,
    size_t event_capacity,
    size_t* event_count) {
    if (decoder == nullptr || report == nullptr || report_length < 8U ||
        events == nullptr || event_count == nullptr) {
        return false;
    }
    *event_count = 0U;
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
        const uint8_t mask = static_cast<uint8_t>(1U << bit);
        if ((decoder->previous_modifiers & mask) != 0U &&
            (report[0U] & mask) == 0U &&
            !append_event(events, event_capacity, event_count,
                          static_cast<uint8_t>(0xE0U + bit), false,
                          report[0U], decoder->caps_lock)) {
            return false;
        }
    }
    for (size_t index = 0U; index < 6U; ++index) {
        const uint8_t usage = decoder->previous_keys[index];
        if (usage > 3U && !contains(report + 2U, usage) &&
            !append_event(events, event_capacity, event_count, usage, false,
                          report[0U], decoder->caps_lock)) {
            return false;
        }
    }
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
        const uint8_t mask = static_cast<uint8_t>(1U << bit);
        if ((decoder->previous_modifiers & mask) == 0U &&
            (report[0U] & mask) != 0U &&
            !append_event(events, event_capacity, event_count,
                          static_cast<uint8_t>(0xE0U + bit), true,
                          report[0U], decoder->caps_lock)) {
            return false;
        }
    }
    for (size_t index = 0U; index < 6U; ++index) {
        const uint8_t usage = report[index + 2U];
        if (usage <= 3U || contains(decoder->previous_keys, usage)) continue;
        if (usage == 0x39U) decoder->caps_lock = !decoder->caps_lock;
        if (!append_event(events, event_capacity, event_count, usage, true,
                          report[0U], decoder->caps_lock)) {
            return false;
        }
    }
    decoder->previous_modifiers = report[0U];
    for (size_t index = 0U; index < 6U; ++index) {
        decoder->previous_keys[index] = report[index + 2U];
    }
    return true;
}

} // namespace drivers::usb

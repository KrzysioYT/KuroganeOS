#include <cassert>
#include <cstdio>
#include <cstring>

#include "../kernel/drivers/usb/protocol.hpp"

using drivers::keyboard::KeyCode;
using drivers::keyboard::KeyEvent;
using drivers::usb::KeyboardDecoder;
using drivers::usb::MAXIMUM_KEYBOARD_EVENTS_PER_REPORT;
using drivers::usb::decode_boot_keyboard_report;

static bool same_state(const KeyboardDecoder& left, const KeyboardDecoder& right) {
    return left.previous_modifiers == right.previous_modifiers &&
        left.caps_lock == right.caps_lock &&
        std::memcmp(left.previous_keys, right.previous_keys, 6U) == 0;
}

int main() {
    KeyboardDecoder decoder{};
    KeyEvent events[MAXIMUM_KEYBOARD_EVENTS_PER_REPORT]{};
    size_t count = 99U;
    const uint8_t caps[] = {0, 0, 0x39, 0, 0, 0, 0, 0};
    events[0].key = KeyCode::F12;
    assert(!decode_boot_keyboard_report(
        &decoder, caps, sizeof(caps), events, 0U, &count));
    assert(count == 0U && !decoder.caps_lock);
    assert(events[0].key == KeyCode::F12);
    assert(decode_boot_keyboard_report(
        &decoder, caps, sizeof(caps), events, 1U, &count));
    assert(count == 1U && decoder.caps_lock);
    assert(events[0].key == KeyCode::CapsLock && events[0].pressed);
    assert(decode_boot_keyboard_report(
        &decoder, caps, sizeof(caps), events, 1U, &count));
    assert(count == 0U && decoder.caps_lock);

    decoder = {};
    const uint8_t shift_a[] = {2, 0, 4, 0, 0, 0, 0, 0};
    const KeyboardDecoder empty = decoder;
    events[0].key = KeyCode::F12;
    assert(!decode_boot_keyboard_report(
        &decoder, shift_a, sizeof(shift_a), events, 1U, &count));
    assert(count == 0U && same_state(decoder, empty));
    assert(events[0].key == KeyCode::F12);
    assert(decode_boot_keyboard_report(
        &decoder, shift_a, sizeof(shift_a), events, 20U, &count));
    assert(count == 2U && events[1].character == 'A');

    const uint8_t rollover[] = {0, 0, 1, 1, 1, 1, 1, 1};
    assert(decode_boot_keyboard_report(
        &decoder, rollover, sizeof(rollover), events, 20U, &count));
    assert(count == 1U && events[0].key == KeyCode::LeftShift &&
           !events[0].pressed && decoder.previous_keys[0] == 4U);
    assert(decode_boot_keyboard_report(
        &decoder, rollover, sizeof(rollover), events, 20U, &count));
    assert(count == 0U);
    const KeyboardDecoder held = decoder;
    for (uint8_t error = 2U; error <= 3U; ++error) {
        const uint8_t invalid[] = {0, 0, error, error, error, error, error, error};
        count = 99U;
        events[0].key = KeyCode::F12;
        assert(!decode_boot_keyboard_report(
            &decoder, invalid, sizeof(invalid), events, 20U, &count));
        assert(count == 0U && same_state(decoder, held));
        assert(events[0].key == KeyCode::F12);
    }
    assert(decode_boot_keyboard_report(
        &decoder, shift_a, sizeof(shift_a), events, 20U, &count));
    assert(count == 1U && events[0].key == KeyCode::LeftShift &&
           events[0].pressed);
    const uint8_t released[8]{};
    assert(decode_boot_keyboard_report(
        &decoder, released, sizeof(released), events, 20U, &count));
    assert(count == 2U && !events[0].pressed && !events[1].pressed);

    decoder = {};
    const uint8_t duplicate[] = {0, 0, 0x39, 0x39, 4, 4, 0, 0};
    assert(decode_boot_keyboard_report(
        &decoder, duplicate, sizeof(duplicate), events, 20U, &count));
    assert(count == 2U && decoder.caps_lock);
    assert(events[1].key == KeyCode::A && events[1].character == 'A');
    assert(decode_boot_keyboard_report(
        &decoder, released, sizeof(released), events, 20U, &count));
    assert(count == 2U && !events[0].pressed && !events[1].pressed);

    decoder = {};
    const uint8_t maximum_held[] = {0xFF, 0, 4, 5, 6, 7, 8, 9};
    const uint8_t replacement[] = {0, 0, 10, 11, 12, 13, 14, 15};
    assert(decode_boot_keyboard_report(
        &decoder, maximum_held, sizeof(maximum_held), events, 20U, &count));
    assert(count == 14U);
    const KeyboardDecoder before = decoder;
    KeyEvent untouched[MAXIMUM_KEYBOARD_EVENTS_PER_REPORT]{};
    std::memcpy(untouched, events, sizeof(events));
    assert(!decode_boot_keyboard_report(
        &decoder, replacement, sizeof(replacement), events, 19U, &count));
    assert(count == 0U && same_state(decoder, before));
    assert(std::memcmp(untouched, events, sizeof(events)) == 0);
    assert(decode_boot_keyboard_report(
        &decoder, replacement, sizeof(replacement), events, 20U, &count));
    assert(count == MAXIMUM_KEYBOARD_EVENTS_PER_REPORT);
    for (size_t index = 0U; index < 14U; ++index) assert(!events[index].pressed);
    for (size_t index = 14U; index < 20U; ++index) assert(events[index].pressed);
    assert(!decode_boot_keyboard_report(
        &decoder, nullptr, 0U, events, 20U, &count));
    assert(count == 0U);
    std::puts("USB keyboard transactional decoding: PASS");
    return 0;
}

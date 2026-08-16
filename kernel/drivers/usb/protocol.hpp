#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../keyboard.hpp"

namespace drivers::usb {

struct HidBootKeyboardInterface {
    uint8_t configuration_value;
    uint8_t interface_number;
    uint8_t endpoint_address;
    uint16_t maximum_packet_size;
    uint8_t interval;
};

bool find_boot_keyboard_interface(
    const uint8_t* descriptors,
    size_t length,
    HidBootKeyboardInterface* output);

struct KeyboardDecoder {
    uint8_t previous_modifiers;
    uint8_t previous_keys[6];
    bool caps_lock;
};

constexpr size_t MAXIMUM_KEYBOARD_EVENTS_PER_REPORT = 20U;

void reset_keyboard_decoder(KeyboardDecoder* decoder);
bool decode_boot_keyboard_report(
    KeyboardDecoder* decoder,
    const uint8_t* report,
    size_t report_length,
    keyboard::KeyEvent* events,
    size_t event_capacity,
    size_t* event_count);

} // namespace drivers::usb

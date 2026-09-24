#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../keyboard.hpp"
#include "../mouse_protocol.hpp"

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
// Allocation-free, all-or-nothing publication: on failure decoder/events are
// unchanged and *event_count is zero (when supplied). Rollover preserves the
// non-modifier set while applying valid modifier changes.
bool decode_boot_keyboard_report(
    KeyboardDecoder* decoder,
    const uint8_t* report,
    size_t report_length,
    keyboard::KeyEvent* events,
    size_t event_capacity,
    size_t* event_count);

struct HidBootMouseInterface {
    uint8_t configuration_value;
    uint8_t interface_number;
    uint8_t endpoint_address;
    uint16_t maximum_packet_size;
    uint8_t interval;
};

bool find_boot_mouse_interface(
    const uint8_t* descriptors,
    size_t length,
    HidBootMouseInterface* output);

struct MouseDecoder {
    uint8_t previous_buttons;
};

void reset_mouse_decoder(MouseDecoder* decoder);
// HID boot mouse reports are decoded transactionally into the shared mouse
// sample contract. The boot protocol defines buttons + X + Y; wheel remains
// zero until a report-protocol extension is explicitly negotiated.
bool decode_boot_mouse_report(
    MouseDecoder* decoder,
    const uint8_t* report,
    size_t report_length,
    mouse::Sample* sample);

} // namespace drivers::usb

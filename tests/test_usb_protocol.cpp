#include <cassert>
#include <cstdint>
#include <iostream>

#include "../kernel/drivers/usb/protocol.hpp"

int main() {
    const uint8_t configuration[] = {
        9, 2, 34, 0, 1, 1, 0, 0x80, 50,
        9, 4, 0, 0, 1, 3, 1, 1, 0,
        9, 0x21, 0x11, 0x01, 0, 1, 0x22, 63, 0,
        7, 5, 0x81, 3, 8, 0, 10,
    };
    drivers::usb::HidBootKeyboardInterface interface{};
    assert(drivers::usb::find_boot_keyboard_interface(
        configuration, sizeof(configuration), &interface));
    assert(interface.configuration_value == 1U);
    assert(interface.interface_number == 0U);
    assert(interface.endpoint_address == 0x81U);
    assert(interface.maximum_packet_size == 8U);
    assert(interface.interval == 10U);

    drivers::usb::KeyboardDecoder decoder{};
    drivers::keyboard::KeyEvent events[
        drivers::usb::MAXIMUM_KEYBOARD_EVENTS_PER_REPORT]{};
    size_t count = 0U;
    const uint8_t shift_a[] = {0x02, 0, 0x04, 0, 0, 0, 0, 0};
    assert(drivers::usb::decode_boot_keyboard_report(
        &decoder, shift_a, sizeof(shift_a), events,
        sizeof(events) / sizeof(events[0]), &count));
    assert(count == 2U);
    assert(events[0].key == drivers::keyboard::KeyCode::LeftShift);
    assert(events[1].key == drivers::keyboard::KeyCode::A);
    assert(events[1].character == 'A' && events[1].pressed);

    const uint8_t f12[] = {0, 0, 0x45, 0, 0, 0, 0, 0};
    assert(drivers::usb::decode_boot_keyboard_report(
        &decoder, f12, sizeof(f12), events,
        sizeof(events) / sizeof(events[0]), &count));
    assert(count == 3U);
    assert(events[2].key == drivers::keyboard::KeyCode::F12);
    assert(events[2].pressed && events[2].character == 0);

    uint8_t malformed[sizeof(configuration)]{};
    for (size_t index = 0U; index < sizeof(configuration); ++index) {
        malformed[index] = configuration[index];
    }
    malformed[9U] = 0U;
    assert(!drivers::usb::find_boot_keyboard_interface(
        malformed, sizeof(malformed), &interface));

    uint8_t alternate_interface[sizeof(configuration)]{};
    for (size_t index = 0U; index < sizeof(configuration); ++index) {
        alternate_interface[index] = configuration[index];
    }
    alternate_interface[13U] = 1U;
    assert(!drivers::usb::find_boot_keyboard_interface(
        alternate_interface, sizeof(alternate_interface), &interface));

    uint8_t endpoint_zero[sizeof(configuration)]{};
    for (size_t index = 0U; index < sizeof(configuration); ++index) {
        endpoint_zero[index] = configuration[index];
    }
    endpoint_zero[29U] = 0x80U;
    assert(!drivers::usb::find_boot_keyboard_interface(
        endpoint_zero, sizeof(endpoint_zero), &interface));

    uint8_t zero_interval[sizeof(configuration)]{};
    for (size_t index = 0U; index < sizeof(configuration); ++index) {
        zero_interval[index] = configuration[index];
    }
    zero_interval[33U] = 0U;
    assert(!drivers::usb::find_boot_keyboard_interface(
        zero_interval, sizeof(zero_interval), &interface));

    uint8_t oversized_endpoint[sizeof(configuration)]{};
    for (size_t index = 0U; index < sizeof(configuration); ++index) {
        oversized_endpoint[index] = configuration[index];
    }
    oversized_endpoint[31U] = 0x01U;
    oversized_endpoint[32U] = 0x08U;
    assert(!drivers::usb::find_boot_keyboard_interface(
        oversized_endpoint, sizeof(oversized_endpoint), &interface));
    std::cout << "USB descriptor and HID tests: PASS\n";
    return 0;
}

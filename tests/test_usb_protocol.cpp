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
    alternate_interface[12U] = 1U;
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
    const uint8_t mouse_configuration[] = {
        9, 2, 34, 0, 1, 1, 0, 0x80, 50,
        9, 4, 2, 0, 1, 3, 1, 2, 0,
        9, 0x21, 0x11, 0x01, 0, 1, 0x22, 50, 0,
        7, 5, 0x82, 3, 4, 0, 5,
    };
    drivers::usb::HidBootMouseInterface mouse_interface{};
    assert(drivers::usb::find_boot_mouse_interface(
        mouse_configuration, sizeof(mouse_configuration), &mouse_interface));
    assert(mouse_interface.configuration_value == 1U);
    assert(mouse_interface.interface_number == 2U);
    assert(mouse_interface.endpoint_address == 0x82U);
    assert(mouse_interface.maximum_packet_size == 4U);
    assert(mouse_interface.interval == 5U);

    uint8_t keyboard_not_mouse[sizeof(mouse_configuration)]{};
    for (size_t index = 0U; index < sizeof(mouse_configuration); ++index) {
        keyboard_not_mouse[index] = mouse_configuration[index];
    }
    keyboard_not_mouse[16U] = 1U;
    assert(!drivers::usb::find_boot_mouse_interface(
        keyboard_not_mouse, sizeof(keyboard_not_mouse), &mouse_interface));

    uint8_t mouse_alternate[sizeof(mouse_configuration)]{};
    uint8_t mouse_endpoint_zero[sizeof(mouse_configuration)]{};
    uint8_t mouse_zero_interval[sizeof(mouse_configuration)]{};
    uint8_t mouse_short_endpoint[sizeof(mouse_configuration)]{};
    uint8_t mouse_oversized_endpoint[sizeof(mouse_configuration)]{};
    for (size_t index = 0U; index < sizeof(mouse_configuration); ++index) {
        mouse_alternate[index] = mouse_configuration[index];
        mouse_endpoint_zero[index] = mouse_configuration[index];
        mouse_zero_interval[index] = mouse_configuration[index];
        mouse_short_endpoint[index] = mouse_configuration[index];
        mouse_oversized_endpoint[index] = mouse_configuration[index];
    }
    mouse_alternate[12U] = 1U;
    mouse_endpoint_zero[29U] = 0x80U;
    mouse_zero_interval[33U] = 0U;
    mouse_short_endpoint[31U] = 2U;
    mouse_short_endpoint[32U] = 0U;
    mouse_oversized_endpoint[31U] = 0x01U;
    mouse_oversized_endpoint[32U] = 0x04U;
    assert(!drivers::usb::find_boot_mouse_interface(
        mouse_alternate, sizeof(mouse_alternate), &mouse_interface));
    assert(!drivers::usb::find_boot_mouse_interface(
        mouse_endpoint_zero, sizeof(mouse_endpoint_zero), &mouse_interface));
    assert(!drivers::usb::find_boot_mouse_interface(
        mouse_zero_interval, sizeof(mouse_zero_interval), &mouse_interface));
    assert(!drivers::usb::find_boot_mouse_interface(
        mouse_short_endpoint, sizeof(mouse_short_endpoint), &mouse_interface));
    assert(!drivers::usb::find_boot_mouse_interface(
        mouse_oversized_endpoint, sizeof(mouse_oversized_endpoint),
        &mouse_interface));

    drivers::usb::MouseDecoder mouse_decoder{};
    drivers::mouse::Sample mouse_sample{11, 12, 3, 7U, 7U};
    const uint8_t short_mouse_report[] = {drivers::mouse::Left, 1U};
    assert(!drivers::usb::decode_boot_mouse_report(
        &mouse_decoder, short_mouse_report, sizeof(short_mouse_report),
        &mouse_sample));
    assert(mouse_decoder.previous_buttons == 0U);
    assert(mouse_sample.delta_x == 11 && mouse_sample.delta_y == 12 &&
        mouse_sample.wheel == 3 && mouse_sample.buttons == 7U &&
        mouse_sample.changed_buttons == 7U);

    const uint8_t press_and_move[] = {drivers::mouse::Left, 5U, 0xFDU};
    assert(drivers::usb::decode_boot_mouse_report(
        &mouse_decoder, press_and_move, sizeof(press_and_move), &mouse_sample));
    assert(mouse_sample.delta_x == 5 && mouse_sample.delta_y == -3);
    assert(mouse_sample.wheel == 0);
    assert(mouse_sample.buttons == drivers::mouse::Left);
    assert(mouse_sample.changed_buttons == drivers::mouse::Left);

    const uint8_t held_and_move[] = {drivers::mouse::Left, 0xFFU, 2U, 0x7FU};
    assert(drivers::usb::decode_boot_mouse_report(
        &mouse_decoder, held_and_move, sizeof(held_and_move), &mouse_sample));
    assert(mouse_sample.delta_x == -1 && mouse_sample.delta_y == 2);
    assert(mouse_sample.wheel == 0);
    assert(mouse_sample.changed_buttons == 0U);

    const uint8_t release[] = {0U, 0U, 0U};
    assert(drivers::usb::decode_boot_mouse_report(
        &mouse_decoder, release, sizeof(release), &mouse_sample));
    assert(mouse_sample.buttons == 0U);
    assert(mouse_sample.changed_buttons == drivers::mouse::Left);

    std::cout << "USB descriptor and HID tests: PASS\n";
    return 0;
}

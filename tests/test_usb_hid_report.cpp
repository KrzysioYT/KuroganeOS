#include "../kernel/drivers/usb/hid_report.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
    using namespace drivers::usb::hid;

    // Generic desktop mouse: 3 buttons, relative X/Y and wheel.
    const uint8_t relative_mouse[] = {
        0x05,0x01, 0x09,0x02, 0xA1,0x01,
        0x09,0x01, 0xA1,0x00,
        0x05,0x09, 0x19,0x01, 0x29,0x03,
        0x15,0x00, 0x25,0x01, 0x95,0x03, 0x75,0x01, 0x81,0x02,
        0x95,0x01, 0x75,0x05, 0x81,0x01,
        0x05,0x01, 0x09,0x30, 0x09,0x31, 0x09,0x38,
        0x15,0x81, 0x25,0x7F, 0x75,0x08, 0x95,0x03, 0x81,0x06,
        0xC0, 0xC0
    };
    PointerReportLayout relative{};
    assert(parse_pointer_report_descriptor(
        relative_mouse, sizeof(relative_mouse), &relative));
    assert(relative.valid && !relative.x.relative == false);
    assert(relative.x.relative && relative.y.relative);
    assert(relative.wheel.present);
    assert(relative.report_bytes == 4U && relative.transfer_bytes == 4U);
    assert(relative.button_count == 3U);

    PointerDecoder decoder{};
    PointerReport report{};
    const uint8_t movement[] = {0x05U, 0x05U, 0xFDU, 0xFFU};
    assert(decode_pointer_report(
        relative, &decoder, movement, sizeof(movement), &report));
    assert(!report.absolute);
    assert(report.x == 5 && report.y == -3 && report.wheel == -1);
    assert(report.buttons == 0x05U && report.changed_buttons == 0x05U);

    // Absolute tablet-style pointer with 16-bit 0..32767 X/Y.
    const uint8_t absolute_tablet[] = {
        0x05,0x01, 0x09,0x02, 0xA1,0x01,
        0x09,0x01, 0xA1,0x00,
        0x05,0x09, 0x19,0x01, 0x29,0x03,
        0x15,0x00, 0x25,0x01, 0x95,0x03, 0x75,0x01, 0x81,0x02,
        0x95,0x01, 0x75,0x05, 0x81,0x01,
        0x05,0x01, 0x09,0x30, 0x09,0x31,
        0x15,0x00, 0x26,0xFF,0x7F,
        0x75,0x10, 0x95,0x02, 0x81,0x02,
        0xC0, 0xC0
    };
    PointerReportLayout absolute{};
    assert(parse_pointer_report_descriptor(
        absolute_tablet, sizeof(absolute_tablet), &absolute));
    assert(absolute.valid && !absolute.x.relative && !absolute.y.relative);
    assert(absolute.report_bytes == 5U);
    assert(absolute.x.logical_minimum == 0 &&
           absolute.x.logical_maximum == 32767);

    reset_pointer_decoder(&decoder);
    const uint8_t absolute_report[] = {
        0x01U, 0x00U, 0x40U, 0xFFU, 0x7FU
    };
    assert(decode_pointer_report(
        absolute, &decoder, absolute_report,
        sizeof(absolute_report), &report));
    assert(report.absolute);
    assert(report.x == 16384 && report.y == 32767);
    assert(report.buttons == 1U && report.changed_buttons == 1U);

    // Report IDs are part of the wire report but not field bit offsets.
    const uint8_t report_id_mouse[] = {
        0x05,0x01, 0x09,0x02, 0xA1,0x01,
        0x85,0x07,
        0x05,0x09, 0x19,0x01, 0x29,0x01,
        0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x01, 0x81,0x02,
        0x75,0x07, 0x95,0x01, 0x81,0x01,
        0x05,0x01, 0x09,0x30, 0x09,0x31,
        0x15,0x81, 0x25,0x7F, 0x75,0x08, 0x95,0x02, 0x81,0x06,
        0xC0
    };
    PointerReportLayout with_id{};
    assert(parse_pointer_report_descriptor(
        report_id_mouse, sizeof(report_id_mouse), &with_id));
    assert(with_id.has_report_id && with_id.report_id == 7U);
    const uint8_t with_id_report[] = {7U, 1U, 2U, 0xFEU};
    reset_pointer_decoder(&decoder);
    assert(decode_pointer_report(
        with_id, &decoder, with_id_report,
        sizeof(with_id_report), &report));
    assert(report.x == 2 && report.y == -2 && report.buttons == 1U);


    // Composite HID interface: keyboard ID 1, pointer ID 7, extended ID 9.
    // The keyboard must not poison pointer field offsets; a longer unrelated
    // report must set endpoint transfer size without changing decode minimum.
    const uint8_t composite[] = {
        0x05,0x01, 0x09,0x06, 0xA1,0x01, 0x85,0x01,
        0x05,0x07, 0x19,0xE0, 0x29,0xE7,
        0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x08, 0x81,0x02, 0xC0,
        0x05,0x01, 0x09,0x02, 0xA1,0x01, 0x85,0x07,
        0x05,0x09, 0x19,0x01, 0x29,0x03,
        0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x03, 0x81,0x02,
        0x75,0x05, 0x95,0x01, 0x81,0x01,
        0x05,0x01, 0x09,0x30, 0x09,0x31,
        0x15,0x81, 0x25,0x7F, 0x75,0x08, 0x95,0x02, 0x81,0x06,
        0xC0,
        0x06,0x00,0xFF, 0x09,0x01, 0xA1,0x01, 0x85,0x09,
        0x75,0x08, 0x95,0x08, 0x81,0x02, 0xC0
    };
    PointerReportLayout multi{};
    assert(parse_pointer_report_descriptor(composite, sizeof(composite), &multi));
    assert(multi.valid && multi.has_report_id && multi.report_id == 7U);
    assert(multi.report_bytes == 4U && multi.transfer_bytes == 9U);
    assert(multi.x.bit_offset == 8U && multi.y.bit_offset == 16U);
    reset_pointer_decoder(&decoder);
    const uint8_t multi_mouse[] = {7U, 0x03U, 0x06U, 0xFCU};
    assert(decode_pointer_report(
        multi, &decoder, multi_mouse, sizeof(multi_mouse), &report));
    assert(report.x == 6 && report.y == -4);
    assert(report.buttons == 3U && report.changed_buttons == 3U);
    const uint8_t unrelated[] = {9U, 3U, 6U, 0xFCU, 0U, 0U, 0U, 0U, 0U};
    assert(!decode_pointer_report(
        multi, &decoder, unrelated, sizeof(unrelated), &report));
    assert(decoder.previous_buttons == 3U);
    const uint8_t short_mouse[] = {7U, 3U, 6U};
    assert(!decode_pointer_report(
        multi, &decoder, short_mouse, sizeof(short_mouse), &report));
    const uint8_t release_mouse[] = {7U, 0U, 0U, 0U};
    assert(decode_pointer_report(
        multi, &decoder, release_mouse, sizeof(release_mouse), &report));
    assert(report.buttons == 0U && report.changed_buttons == 3U);

    const uint8_t truncated[] = {0x05, 0x01, 0x09};
    assert(!parse_pointer_report_descriptor(
        truncated, sizeof(truncated), &absolute));

    std::cout << "USB HID report pointer parser: PASS\n";
    return 0;
}

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace drivers::usb::hid {

struct Field {
    uint16_t bit_offset;
    uint8_t bit_size;
    int32_t logical_minimum;
    int32_t logical_maximum;
    bool relative;
    bool present;
};

struct PointerReportLayout {
    Field x;
    Field y;
    Field wheel;
    uint16_t buttons_bit_offset;
    uint8_t button_count;
    uint8_t report_id;
    uint16_t report_bytes;
    bool has_report_id;
    bool valid;
};

struct PointerDecoder {
    uint8_t previous_buttons;
};

struct PointerReport {
    bool absolute;
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

bool parse_pointer_report_descriptor(
    const uint8_t* descriptor,
    size_t length,
    PointerReportLayout* output);

void reset_pointer_decoder(PointerDecoder* decoder);

bool decode_pointer_report(
    const PointerReportLayout& layout,
    PointerDecoder* decoder,
    const uint8_t* report,
    size_t report_length,
    PointerReport* output);

} // namespace drivers::usb::hid

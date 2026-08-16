#pragma once

#include <stddef.h>
#include <stdint.h>

namespace drivers::mouse {

enum Button : uint8_t {
    Left = UINT8_C(1) << 0U,
    Right = UINT8_C(1) << 1U,
    Middle = UINT8_C(1) << 2U,
};

struct Sample {
    int16_t delta_x;
    int16_t delta_y;
    int8_t wheel;
    uint8_t buttons;
    uint8_t changed_buttons;
};

struct Decoder {
    uint8_t packet[4];
    size_t position;
    size_t packet_size;
    uint8_t previous_buttons;
};

void initialize_decoder(Decoder* decoder, bool wheel_mode);
bool decode_byte(Decoder* decoder, uint8_t value, Sample* out_sample);

} // namespace drivers::mouse

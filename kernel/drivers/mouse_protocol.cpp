#include "mouse_protocol.hpp"

namespace drivers::mouse {

void initialize_decoder(Decoder* decoder, bool wheel_mode) {
    if (decoder == nullptr) return;
    *decoder = {};
    decoder->packet_size = wheel_mode ? 4U : 3U;
}

bool decode_byte(Decoder* decoder, uint8_t value, Sample* out_sample) {
    if (decoder == nullptr || out_sample == nullptr ||
        (decoder->packet_size != 3U && decoder->packet_size != 4U)) {
        return false;
    }
    if (decoder->position == 0U && (value & UINT8_C(0x08)) == 0U) {
        return false;
    }
    decoder->packet[decoder->position++] = value;
    if (decoder->position != decoder->packet_size) return false;
    decoder->position = 0U;
    const uint8_t status = decoder->packet[0];
    if ((status & UINT8_C(0xc0)) != 0U) return false;
    const int16_t delta_x = static_cast<int8_t>(decoder->packet[1]);
    const int16_t delta_y = static_cast<int16_t>(
        -static_cast<int16_t>(static_cast<int8_t>(decoder->packet[2])));
    int8_t wheel = 0;
    if (decoder->packet_size == 4U) {
        uint8_t nibble = decoder->packet[3] & UINT8_C(0x0f);
        if ((nibble & UINT8_C(0x08)) != 0U) nibble |= UINT8_C(0xf0);
        wheel = static_cast<int8_t>(nibble);
    }
    const uint8_t buttons = status & UINT8_C(0x07);
    *out_sample = {
        delta_x,
        delta_y,
        wheel,
        buttons,
        static_cast<uint8_t>(buttons ^ decoder->previous_buttons)
    };
    decoder->previous_buttons = buttons;
    return true;
}

} // namespace drivers::mouse

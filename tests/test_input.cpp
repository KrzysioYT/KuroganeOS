#include "../kernel/input/input.hpp"

int main() {
    drivers::mouse::Decoder decoder{};
    drivers::mouse::initialize_decoder(&decoder, false);
    drivers::mouse::Sample sample{};
    if (drivers::mouse::decode_byte(&decoder, 0x08U, &sample) ||
        drivers::mouse::decode_byte(&decoder, 5U, &sample) ||
        !drivers::mouse::decode_byte(&decoder, 0xfdU, &sample) ||
        sample.delta_x != 5 || sample.delta_y != 3 || sample.wheel != 0 ||
        sample.buttons != 0U) return 1;

    drivers::mouse::initialize_decoder(&decoder, true);
    if (drivers::mouse::decode_byte(&decoder, 0x09U, &sample) ||
        drivers::mouse::decode_byte(&decoder, 0xffU, &sample) ||
        drivers::mouse::decode_byte(&decoder, 2U, &sample) ||
        !drivers::mouse::decode_byte(&decoder, 0x0fU, &sample) ||
        sample.delta_x != -1 || sample.delta_y != -2 ||
        sample.wheel != -1 || sample.buttons != drivers::mouse::Left ||
        sample.changed_buttons != drivers::mouse::Left) return 2;

    if (!input::initialize(100U, 80U) || input::pointer_x() != 49 ||
        input::pointer_y() != 39) return 3;
    drivers::keyboard::KeyEvent key{};
    key.key = drivers::keyboard::KeyCode::A;
    key.character = 'a';
    key.pressed = true;
    if (!input::submit_key(key)) return 4;
    const drivers::mouse::Sample pointer = {
        1000, -1000, 1, drivers::mouse::Left, drivers::mouse::Left
    };
    if (!input::submit_mouse(pointer) || input::pointer_x() != 99 ||
        input::pointer_y() != 0 || input::pointer_buttons() != drivers::mouse::Left ||
        input::pending_events() != 4U) return 5;
    input::Event event{};
    if (!input::try_read(&event) || event.type != input::EventType::KeyDown ||
        event.character != 'a') return 6;
    if (!input::try_read(&event) || event.type != input::EventType::MouseMove ||
        event.x != 99 || event.y != 0) return 7;
    if (!input::try_read(&event) ||
        event.type != input::EventType::MouseButtonDown ||
        event.button != drivers::mouse::Left) return 8;
    if (!input::try_read(&event) || event.type != input::EventType::MouseWheel ||
        event.wheel != 1 || input::try_read(&event)) return 9;

    drivers::mouse::initialize_decoder(&decoder, false);
    if (drivers::mouse::decode_byte(&decoder, 0x00U, &sample) ||
        decoder.position != 0U) return 10;
    return 0;
}

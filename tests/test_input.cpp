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

    /*
     * Pointer motion is intentionally a latest-position stream. Consecutive
     * motion packets must collapse to one event so a busy software desktop
     * never spends frames replaying stale cursor positions. A button event is
     * a semantic boundary and must prevent coalescing across the click.
     */
    if (!input::initialize(100U, 80U)) return 10;
    const drivers::mouse::Sample move_a = {3, 2, 0, 0U, 0U};
    const drivers::mouse::Sample move_b = {4, -1, 0, 0U, 0U};
    if (!input::submit_mouse(move_a) || !input::submit_mouse(move_b) ||
        input::pointer_x() != 56 || input::pointer_y() != 40 ||
        input::pending_events() != 1U) return 11;
    if (!input::try_read(&event) || event.type != input::EventType::MouseMove ||
        event.x != 56 || event.y != 40 || event.delta_x != 4 ||
        event.delta_y != -1 || input::try_read(&event)) return 12;

    const drivers::mouse::Sample press = {
        0, 0, 0, drivers::mouse::Left, drivers::mouse::Left
    };
    const drivers::mouse::Sample drag = {
        2, 0, 0, drivers::mouse::Left, 0U
    };
    if (!input::submit_mouse(press) || !input::submit_mouse(drag) ||
        input::pending_events() != 2U) return 13;
    if (!input::try_read(&event) ||
        event.type != input::EventType::MouseButtonDown ||
        event.button != drivers::mouse::Left) return 14;
    if (!input::try_read(&event) || event.type != input::EventType::MouseMove ||
        event.x != 58 || event.y != 40 ||
        event.buttons != drivers::mouse::Left || input::try_read(&event)) return 15;

    drivers::mouse::initialize_decoder(&decoder, false);
    if (drivers::mouse::decode_byte(&decoder, 0x00U, &sample) ||
        decoder.position != 0U) return 16;
    return 0;
}

#include "../kernel/input/input.hpp"
#include <cassert>
#include <cstdio>

namespace fixture {
drivers::keyboard::KeyEvent keys[2]{};
drivers::mouse::Sample samples[2]{};
size_t key_count = 0U, key_read = 0U, sample_count = 0U, sample_read = 0U;
}
namespace drivers::keyboard {
bool try_read_event(KeyEvent& event) {
    if (fixture::key_read == fixture::key_count) return false;
    event = fixture::keys[fixture::key_read++];
    return true;
}
}
namespace drivers::mouse {
bool try_read_sample(Sample* sample) {
    if (fixture::sample_read == fixture::sample_count) return false;
    *sample = fixture::samples[fixture::sample_read++];
    return true;
}
}

static void pump_backpressure_regression() {
    assert(input::initialize(100U, 80U));
    drivers::keyboard::KeyEvent filler{};
    for (size_t index = 0U; index < input::EVENT_QUEUE_CAPACITY; ++index) {
        assert(input::submit_key(filler));
    }
    fixture::keys[0].key = drivers::keyboard::KeyCode::A;
    fixture::keys[0].pressed = true;
    fixture::keys[1].key = drivers::keyboard::KeyCode::A;
    fixture::keys[1].pressed = false;
    fixture::key_count = 2U;
    fixture::samples[0] = {1, 1, 1, 7U, 7U};
    fixture::samples[1] = {-1, -1, -1, 0U, 7U};
    fixture::sample_count = 2U;
    for (size_t retry = 0U; retry < 3U; ++retry) {
        assert(input::pump() == 0U);
        assert(fixture::key_read == 1U && fixture::sample_read == 0U);
    }
    input::Event event{};
    // Give the keyboard exactly enough room; the first mouse sample stays
    // owned by the pump instead of being lost after dequeue from the driver.
    assert(input::try_read(&event) && input::try_read(&event));
    assert(input::pump() == 2U);
    assert(fixture::key_read == 2U && fixture::sample_read == 1U);
    for (size_t retry = 0U; retry < 3U; ++retry) {
        assert(input::pump() == 0U && fixture::sample_read == 1U);
    }
    for (size_t index = 0U; index < input::EVENT_QUEUE_CAPACITY - 2U; ++index) {
        assert(input::try_read(&event));
    }
    assert(input::try_read(&event) && event.type == input::EventType::KeyDown);
    assert(event.key == drivers::keyboard::KeyCode::A);
    assert(input::try_read(&event) && event.type == input::EventType::KeyUp);
    assert(event.key == drivers::keyboard::KeyCode::A);
    assert(input::pump() == 2U && fixture::sample_read == 2U);
    assert(input::pending_events() == 10U);
    assert(input::pointer_x() == 49 && input::pointer_y() == 39);
    assert(input::pointer_buttons() == 0U);
    assert(input::pump() == 0U && input::pending_events() == 10U);
    std::puts("Input pump retains blocked keyboard and mouse events: PASS");
}

static void mouse_publication_regression() {
    using input::EventType;
    const drivers::mouse::Sample press{1, 1, 1, 7U, 7U};
    drivers::keyboard::KeyEvent key{};
    key.key = drivers::keyboard::KeyCode::A;
    key.pressed = true;
    for (size_t available = 0U; available < 5U; ++available) {
        assert(input::initialize(100U, 80U));
        for (size_t count = available; count < input::EVENT_QUEUE_CAPACITY; ++count) {
            assert(input::submit_key(key));
        }
        const size_t occupied = input::pending_events();
        for (size_t retry = 0U; retry < 3U; ++retry) {
            assert(!input::submit_mouse(press));
            assert(input::pending_events() == occupied);
            assert(input::pointer_x() == 49 && input::pointer_y() == 39);
            assert(input::pointer_buttons() == 0U);
        }
        input::Event event{};
        for (size_t count = 0U; count < occupied; ++count) {
            assert(input::try_read(&event) && event.type == EventType::KeyDown);
        }
        assert(!input::try_read(&event));
        assert(input::submit_mouse(press));
        assert(input::pointer_x() == 50 && input::pointer_y() == 40);
        assert(input::pending_events() == 5U && input::pointer_buttons() == 7U);
        assert(input::try_read(&event) && event.type == EventType::MouseMove);
        constexpr uint8_t buttons[]{1U, 2U, 4U};
        for (uint8_t button : buttons) {
            assert(input::try_read(&event) && event.type == EventType::MouseButtonDown);
            assert(event.button == button && event.x == 50 && event.y == 40);
        }
        assert(input::try_read(&event) && event.type == EventType::MouseWheel);
        assert(!input::try_read(&event));
        for (size_t count = available; count < input::EVENT_QUEUE_CAPACITY; ++count) {
            assert(input::submit_key(key));
        }
        const drivers::mouse::Sample release{-1, -1, -1, 0U, 7U};
        assert(!input::submit_mouse(release));
        assert(input::pending_events() == occupied);
        assert(input::pointer_x() == 50 && input::pointer_y() == 40);
        assert(input::pointer_buttons() == 7U);
        for (size_t count = 0U; count < occupied; ++count) {
            assert(input::try_read(&event) && event.type == EventType::KeyDown);
        }
        assert(input::submit_mouse(release));
        assert(input::try_read(&event) && event.type == EventType::MouseMove);
        for (uint8_t button : buttons) {
            assert(input::try_read(&event) && event.type == EventType::MouseButtonUp);
            assert(event.button == button && event.x == 49 && event.y == 39);
        }
        assert(input::try_read(&event) && event.type == EventType::MouseWheel);
        assert(event.wheel == -1 && !input::try_read(&event));
        assert(input::pointer_buttons() == 0U && input::pointer_x() == 49);
    }
    assert(input::initialize(100U, 80U));
    // Cross both the 256-slot ring boundary and uint16_t sequence wrap.
    for (size_t count = 0U; count < 14000U; ++count) {
        const bool down = (count & 1U) == 0U;
        const drivers::mouse::Sample sample{
            static_cast<int16_t>(down ? 1 : -1), 0, 1,
            static_cast<uint8_t>(down ? 7U : 0U), 7U};
        assert(input::submit_mouse(sample));
        assert(input::pending_events() == 5U);
        input::Event event{};
        for (size_t index = 0U; index < 5U; ++index) assert(input::try_read(&event));
        assert(!input::try_read(&event));
    }
    assert(input::pointer_x() == 49 && input::pointer_buttons() == 0U);
    std::puts("Input mouse publication is retryable and atomic across queue wrap: PASS");
}

int main() {
    const drivers::mouse::Sample early{1, 1, 0, 1U, 1U};
    assert(!input::submit_mouse(early));
    assert(input::pointer_x() == 0 && input::pointer_buttons() == 0U);
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
    mouse_publication_regression();
    pump_backpressure_regression();
    return 0;
}

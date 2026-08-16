#include "mouse.hpp"

#include "keyboard.hpp"
#include "pic.hpp"
#include "../arch/x86_64/interrupts.hpp"

namespace drivers::mouse {
namespace {

constexpr uint16_t DATA_PORT = 0x60U;
constexpr uint16_t STATUS_COMMAND_PORT = 0x64U;
constexpr uint8_t STATUS_OUTPUT_FULL = UINT8_C(1) << 0U;
constexpr uint8_t STATUS_INPUT_FULL = UINT8_C(1) << 1U;
constexpr uint8_t STATUS_AUXILIARY_DATA = UINT8_C(1) << 5U;
constexpr uint8_t COMMAND_ENABLE_SECOND_PORT = 0xa8U;
constexpr uint8_t COMMAND_DISABLE_SECOND_PORT = 0xa7U;
constexpr uint8_t COMMAND_READ_CONFIG = 0x20U;
constexpr uint8_t COMMAND_WRITE_CONFIG = 0x60U;
constexpr uint8_t COMMAND_WRITE_AUXILIARY = 0xd4U;
constexpr uint8_t DEVICE_SET_DEFAULTS = 0xf6U;
constexpr uint8_t DEVICE_ENABLE_REPORTING = 0xf4U;
constexpr uint8_t DEVICE_SET_SAMPLE_RATE = 0xf3U;
constexpr uint8_t DEVICE_GET_ID = 0xf2U;
constexpr uint8_t DEVICE_ACK = 0xfaU;
constexpr size_t IO_TIMEOUT = 200000U;
constexpr size_t MAX_DRAIN_BYTES = 64U;
constexpr size_t SAMPLE_BUFFER_MASK = SAMPLE_BUFFER_CAPACITY - 1U;

static_assert((SAMPLE_BUFFER_CAPACITY & SAMPLE_BUFFER_MASK) == 0U,
              "mouse sample queue must be a power of two");

Sample g_samples[SAMPLE_BUFFER_CAPACITY]{};
uint16_t g_head = 0U;
uint16_t g_tail = 0U;
uint64_t g_dropped = 0U;
Decoder g_decoder{};
bool g_initialized = false;
bool g_controller_configured = false;
bool g_wheel_enabled = false;

void out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

uint8_t in8(uint16_t port) {
    uint8_t value = 0U;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

void io_wait() { out8(0x80U, 0U); }

bool wait_input_empty() {
    for (size_t attempt = 0U; attempt < IO_TIMEOUT; ++attempt) {
        if ((in8(STATUS_COMMAND_PORT) & STATUS_INPUT_FULL) == 0U) return true;
        io_wait();
    }
    return false;
}

bool write_command(uint8_t command) {
    if (!wait_input_empty()) return false;
    out8(STATUS_COMMAND_PORT, command);
    return true;
}

bool write_data(uint8_t value) {
    if (!wait_input_empty()) return false;
    out8(DATA_PORT, value);
    return true;
}

bool read_auxiliary(uint8_t* output) {
    if (output == nullptr) return false;
    for (size_t attempt = 0U; attempt < IO_TIMEOUT; ++attempt) {
        const uint8_t status = in8(STATUS_COMMAND_PORT);
        if ((status & STATUS_OUTPUT_FULL) == 0U) {
            io_wait();
            continue;
        }
        const uint8_t value = in8(DATA_PORT);
        if ((status & STATUS_AUXILIARY_DATA) != 0U) {
            *output = value;
            return true;
        }
        keyboard::process_scancode(value);
    }
    return false;
}

bool send_device(uint8_t command, uint8_t* response = nullptr) {
    for (size_t attempt = 0U; attempt < 3U; ++attempt) {
        if (!write_command(COMMAND_WRITE_AUXILIARY) || !write_data(command)) {
            return false;
        }
        uint8_t value = 0U;
        if (!read_auxiliary(&value)) return false;
        if (value == DEVICE_ACK) {
            if (response != nullptr) *response = value;
            return true;
        }
        if (value != UINT8_C(0xfe)) return false;
    }
    return false;
}

bool set_sample_rate(uint8_t rate) {
    return send_device(DEVICE_SET_SAMPLE_RATE) && send_device(rate);
}

bool configure_controller() {
    if (!write_command(COMMAND_ENABLE_SECOND_PORT) ||
        !write_command(COMMAND_READ_CONFIG)) return false;
    uint8_t config = 0U;
    bool received = false;
    for (size_t attempt = 0U; attempt < IO_TIMEOUT; ++attempt) {
        if ((in8(STATUS_COMMAND_PORT) & STATUS_OUTPUT_FULL) != 0U) {
            config = in8(DATA_PORT);
            received = true;
            break;
        }
        io_wait();
    }
    if (!received) return false;
    config = static_cast<uint8_t>(
        (config | (UINT8_C(1) << 1U)) & ~(UINT8_C(1) << 5U));
    if (!write_command(COMMAND_WRITE_CONFIG) || !write_data(config) ||
        !send_device(DEVICE_SET_DEFAULTS)) return false;

    g_wheel_enabled = false;
    if (set_sample_rate(200U) && set_sample_rate(100U) &&
        set_sample_rate(80U) && send_device(DEVICE_GET_ID)) {
        uint8_t identifier = 0U;
        if (read_auxiliary(&identifier) && identifier == 3U) {
            g_wheel_enabled = true;
        }
    }
    initialize_decoder(&g_decoder, g_wheel_enabled);
    return send_device(DEVICE_ENABLE_REPORTING);
}

void push_sample(const Sample& sample) {
    const uint16_t head = __atomic_load_n(&g_head, __ATOMIC_RELAXED);
    const uint16_t tail = __atomic_load_n(&g_tail, __ATOMIC_ACQUIRE);
    if (static_cast<uint16_t>(head - tail) >= SAMPLE_BUFFER_CAPACITY) {
        __atomic_fetch_add(&g_dropped, UINT64_C(1), __ATOMIC_RELAXED);
        return;
    }
    g_samples[head & SAMPLE_BUFFER_MASK] = sample;
    __atomic_store_n(&g_head, static_cast<uint16_t>(head + 1U), __ATOMIC_RELEASE);
}

size_t drain_controller() {
    size_t processed = 0U;
    for (size_t attempt = 0U; attempt < MAX_DRAIN_BYTES; ++attempt) {
        const uint8_t status = in8(STATUS_COMMAND_PORT);
        if ((status & STATUS_OUTPUT_FULL) == 0U) break;
        const uint8_t value = in8(DATA_PORT);
        if ((status & STATUS_AUXILIARY_DATA) != 0U) {
            process_byte(value);
            ++processed;
        } else {
            keyboard::process_scancode(value);
        }
    }
    return processed;
}

} // namespace

bool initialize() {
    pic::mask(12U);
    g_head = 0U;
    g_tail = 0U;
    g_dropped = 0U;
    g_initialized = false;
    if (!arch::x86_64::interrupts::register_irq_handler(12U, handle_irq)) {
        return false;
    }
    g_controller_configured = configure_controller();
    g_initialized = g_controller_configured && pic::unmask(12U);
    if (!g_initialized) {
        arch::x86_64::interrupts::unregister_irq_handler(12U);
    }
    return g_initialized;
}

void shutdown() {
    pic::mask(12U);
    static_cast<void>(write_command(COMMAND_DISABLE_SECOND_PORT));
    arch::x86_64::interrupts::unregister_irq_handler(12U);
    g_initialized = false;
    g_controller_configured = false;
}

bool initialized() { return g_initialized; }
bool controller_configured() { return g_controller_configured; }
bool wheel_enabled() { return g_wheel_enabled; }
void handle_irq() { static_cast<void>(drain_controller()); }

size_t poll() {
    const bool restore = arch::x86_64::interrupts::enabled();
    arch::x86_64::interrupts::disable();
    const size_t processed = drain_controller();
    if (restore) arch::x86_64::interrupts::enable();
    return processed;
}

void process_byte(uint8_t value) {
    Sample sample{};
    if (decode_byte(&g_decoder, value, &sample)) push_sample(sample);
}

bool try_read_sample(Sample* out_sample) {
    if (out_sample == nullptr) return false;
    uint16_t tail = __atomic_load_n(&g_tail, __ATOMIC_RELAXED);
    uint16_t head = __atomic_load_n(&g_head, __ATOMIC_ACQUIRE);
    if (tail == head) {
        static_cast<void>(poll());
        tail = __atomic_load_n(&g_tail, __ATOMIC_RELAXED);
        head = __atomic_load_n(&g_head, __ATOMIC_ACQUIRE);
        if (tail == head) return false;
    }
    *out_sample = g_samples[tail & SAMPLE_BUFFER_MASK];
    __atomic_store_n(&g_tail, static_cast<uint16_t>(tail + 1U), __ATOMIC_RELEASE);
    return true;
}

size_t pending_samples() {
    const uint16_t head = __atomic_load_n(&g_head, __ATOMIC_ACQUIRE);
    const uint16_t tail = __atomic_load_n(&g_tail, __ATOMIC_ACQUIRE);
    return static_cast<size_t>(static_cast<uint16_t>(head - tail));
}

uint64_t dropped_samples() {
    return __atomic_load_n(&g_dropped, __ATOMIC_RELAXED);
}

} // namespace drivers::mouse

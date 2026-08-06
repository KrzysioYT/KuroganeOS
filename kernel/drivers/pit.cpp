#include "pit.hpp"

#include "../arch/x86_64/interrupts.hpp"
#include "pic.hpp"

namespace drivers::pit {

namespace {

constexpr uint16_t CHANNEL0_DATA = 0x40;
constexpr uint16_t MODE_COMMAND = 0x43;
constexpr uint8_t CHANNEL0_LO_HI_MODE3 = 0x36;

alignas(8) static uint64_t g_ticks = 0;
static uint32_t g_frequency_hz = 0;
static uint16_t g_divisor = 0;
static bool g_initialized = false;

inline void out8(uint16_t port, uint8_t value) {
    __asm__ volatile(
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
        : "memory");
}

} // namespace

bool initialize(uint32_t requested_hz) {
    if (requested_hz == 0 ||
        requested_hz > INPUT_FREQUENCY_HZ) {
        return false;
    }

    const uint32_t calculated_divisor =
        (INPUT_FREQUENCY_HZ + requested_hz / 2) /
        requested_hz;
    if (calculated_divisor == 0 ||
        calculated_divisor > 0xFFFF) {
        return false;
    }

    if (!arch::x86_64::interrupts::register_irq_handler(
            0,
            handle_irq)) {
        return false;
    }

    const uint16_t new_divisor =
        static_cast<uint16_t>(calculated_divisor);
    out8(MODE_COMMAND, CHANNEL0_LO_HI_MODE3);
    out8(
        CHANNEL0_DATA,
        static_cast<uint8_t>(new_divisor & 0xFF));
    out8(
        CHANNEL0_DATA,
        static_cast<uint8_t>((new_divisor >> 8) & 0xFF));

    __atomic_store_n(&g_ticks, static_cast<uint64_t>(0), __ATOMIC_RELAXED);
    g_divisor = new_divisor;
    g_frequency_hz = INPUT_FREQUENCY_HZ / calculated_divisor;
    g_initialized = drivers::pic::unmask(0);
    if (!g_initialized) {
        arch::x86_64::interrupts::unregister_irq_handler(0);
    }
    return g_initialized;
}

void shutdown() {
    drivers::pic::mask(0);
    arch::x86_64::interrupts::unregister_irq_handler(0);
    g_initialized = false;
    g_frequency_hz = 0;
    g_divisor = 0;
}

bool initialized() {
    return g_initialized;
}

uint32_t frequency_hz() {
    return g_frequency_hz;
}

uint16_t divisor() {
    return g_divisor;
}

uint64_t ticks() {
    return __atomic_load_n(&g_ticks, __ATOMIC_RELAXED);
}

void reset_ticks() {
    __atomic_store_n(&g_ticks, static_cast<uint64_t>(0), __ATOMIC_RELAXED);
}

void handle_irq() {
    __atomic_fetch_add(
        &g_ticks,
        static_cast<uint64_t>(1),
        __ATOMIC_RELAXED);
}

} // namespace drivers::pit

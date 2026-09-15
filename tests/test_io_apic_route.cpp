#include <assert.h>
#include <stdint.h>

#include "../kernel/arch/x86_64/io_apic_route.hpp"

namespace io_apic = arch::x86_64::io_apic;

int main() {
    assert(!io_apic::validate({0x20U, 0U, io_apic::TriggerMode::Edge,
        io_apic::Polarity::ActiveHigh, true}));
    assert(!io_apic::validate({0x80U, 0U, io_apic::TriggerMode::Edge,
        io_apic::Polarity::ActiveHigh, true}));
    assert(!io_apic::validate({0xF0U, 0U, io_apic::TriggerMode::Edge,
        io_apic::Polarity::ActiveHigh, true}));

    const io_apic::Route expected{
        0x51U, 3U, io_apic::TriggerMode::Level,
        io_apic::Polarity::ActiveLow, false};
    assert(io_apic::validate(expected));
    const uint32_t low = io_apic::encode_low(expected);
    const uint32_t high = io_apic::encode_high(expected);
    assert(low == (UINT32_C(0x51) | (UINT32_C(1) << 13U) |
        (UINT32_C(1) << 15U)));
    assert(high == (UINT32_C(3) << 24U));

    io_apic::Route decoded{};
    assert(io_apic::decode(low, high, &decoded));
    assert(decoded.vector == expected.vector);
    assert(decoded.destination_apic_id == expected.destination_apic_id);
    assert(decoded.trigger == expected.trigger);
    assert(decoded.polarity == expected.polarity);
    assert(decoded.masked == expected.masked);
    assert(!io_apic::decode(low | (UINT32_C(1) << 11U), high, &decoded));
    assert(!io_apic::decode(low | (UINT32_C(1) << 8U), high, &decoded));
    assert(!io_apic::decode(low, high, nullptr));

    const io_apic::Route masked = {
        expected.vector, expected.destination_apic_id,
        expected.trigger, expected.polarity, true};
    assert((io_apic::encode_low(masked) & (UINT32_C(1) << 16U)) != 0U);
    return 0;
}

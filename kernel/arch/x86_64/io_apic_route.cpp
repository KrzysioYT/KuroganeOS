#include "io_apic_route.hpp"

namespace arch::x86_64::io_apic {
namespace {

constexpr uint32_t kPolarityBit = UINT32_C(1) << 13U;
constexpr uint32_t kTriggerBit = UINT32_C(1) << 15U;
constexpr uint32_t kMaskBit = UINT32_C(1) << 16U;
constexpr uint32_t kDeliveryModeMask = UINT32_C(0x7) << 8U;
constexpr uint32_t kDestinationModeBit = UINT32_C(1) << 11U;

} // namespace

bool validate(const Route& route) {
    const uint8_t vector = route.vector;
    if (vector < FIRST_HARDWARE_VECTOR ||
        vector > LAST_HARDWARE_VECTOR ||
        vector == RESERVED_SYSCALL_VECTOR) {
        return false;
    }
    return route.trigger == TriggerMode::Edge ||
        route.trigger == TriggerMode::Level;
}

uint32_t encode_low(const Route& route) {
    if (!validate(route)) return 0U;
    uint32_t value = route.vector;
    if (route.polarity == Polarity::ActiveLow) value |= kPolarityBit;
    if (route.trigger == TriggerMode::Level) value |= kTriggerBit;
    if (route.masked) value |= kMaskBit;
    return value;
}

uint32_t encode_high(const Route& route) {
    if (!validate(route)) return 0U;
    return static_cast<uint32_t>(route.destination_apic_id) << 24U;
}

bool decode(uint32_t low, uint32_t high, Route* output) {
    if (output == nullptr) return false;
    if ((low & (kDeliveryModeMask | kDestinationModeBit)) != 0U) {
        return false;
    }
    Route route{};
    route.vector = static_cast<uint8_t>(low & 0xFFU);
    route.destination_apic_id = static_cast<uint8_t>(high >> 24U);
    route.trigger = (low & kTriggerBit) != 0U
        ? TriggerMode::Level : TriggerMode::Edge;
    route.polarity = (low & kPolarityBit) != 0U
        ? Polarity::ActiveLow : Polarity::ActiveHigh;
    route.masked = (low & kMaskBit) != 0U;
    if (!validate(route)) return false;
    *output = route;
    return true;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::InvalidArgument: return "invalid argument";
        case Status::InvalidVector: return "invalid hardware vector";
        case Status::UnsupportedDeliveryMode: return "unsupported delivery mode";
        case Status::UnsupportedDestinationMode: return "unsupported destination mode";
    }
    return "unknown I/O APIC route status";
}

} // namespace arch::x86_64::io_apic

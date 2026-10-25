#include "io_apic_irq.hpp"

namespace arch::x86_64::io_apic {
namespace {

constexpr uint8_t ISA_IRQ_COUNT = 16U;
constexpr uint16_t DEFINED_OVERRIDE_FLAGS = 0x000FU;

bool decode_override_flags(
    uint16_t flags,
    TriggerMode* trigger,
    Polarity* polarity) {
    if (trigger == nullptr || polarity == nullptr ||
        (flags & static_cast<uint16_t>(~DEFINED_OVERRIDE_FLAGS)) != 0U) {
        return false;
    }

    const uint16_t polarity_bits = flags & 0x0003U;
    if (polarity_bits == 0U || polarity_bits == 1U) {
        *polarity = Polarity::ActiveHigh;
    } else if (polarity_bits == 3U) {
        *polarity = Polarity::ActiveLow;
    } else {
        return false;
    }

    const uint16_t trigger_bits = (flags >> 2U) & 0x0003U;
    if (trigger_bits == 0U || trigger_bits == 1U) {
        *trigger = TriggerMode::Edge;
    } else if (trigger_bits == 3U) {
        *trigger = TriggerMode::Level;
    } else {
        return false;
    }
    return true;
}

} // namespace

LegacyStatus resolve_legacy_irq(
    uint8_t legacy_irq,
    const acpi::InterruptOverride* overrides,
    size_t override_count,
    LegacyRoute* output) {
    if (output == nullptr ||
        (override_count != 0U && overrides == nullptr) ||
        override_count > acpi::MAXIMUM_OVERRIDES) {
        return LegacyStatus::InvalidArgument;
    }
    if (legacy_irq >= ISA_IRQ_COUNT) return LegacyStatus::InvalidIrq;

    LegacyRoute result{
        static_cast<uint32_t>(legacy_irq),
        TriggerMode::Edge,
        Polarity::ActiveHigh,
    };
    bool matched = false;
    for (size_t index = 0U; index < override_count; ++index) {
        const auto& entry = overrides[index];
        if (entry.bus != 0U) continue;
        if (entry.source_irq >= ISA_IRQ_COUNT) {
            return LegacyStatus::InvalidOverride;
        }
        if (entry.source_irq != legacy_irq) continue;
        if (matched) return LegacyStatus::ConflictingOverride;

        if (!decode_override_flags(entry.flags, &result.trigger, &result.polarity)) {
            return LegacyStatus::InvalidOverride;
        }
        result.global_system_interrupt = entry.global_interrupt;
        matched = true;
    }
    *output = result;
    return LegacyStatus::Ok;
}

const char* legacy_status_message(LegacyStatus status) {
    switch (status) {
        case LegacyStatus::Ok: return "ok";
        case LegacyStatus::InvalidArgument: return "invalid argument";
        case LegacyStatus::InvalidIrq: return "legacy IRQ is outside ISA range";
        case LegacyStatus::InvalidOverride: return "invalid ACPI interrupt override";
        case LegacyStatus::ConflictingOverride:
            return "conflicting ACPI interrupt overrides";
    }
    return "unknown legacy IRQ status";
}

} // namespace arch::x86_64::io_apic

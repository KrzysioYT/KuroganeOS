#pragma once

#include <stddef.h>
#include <stdint.h>

#include "acpi.hpp"
#include "io_apic_route.hpp"

namespace arch::x86_64::io_apic {

enum class LegacyStatus : uint8_t {
    Ok = 0,
    InvalidArgument,
    InvalidIrq,
    InvalidOverride,
    ConflictingOverride,
};

struct LegacyRoute {
    uint32_t global_system_interrupt;
    TriggerMode trigger;
    Polarity polarity;
};

LegacyStatus resolve_legacy_irq(
    uint8_t legacy_irq,
    const acpi::InterruptOverride* overrides,
    size_t override_count,
    LegacyRoute* output);

const char* legacy_status_message(LegacyStatus status);

} // namespace arch::x86_64::io_apic

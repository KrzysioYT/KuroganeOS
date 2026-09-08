#pragma once

#include <stddef.h>
#include <stdint.h>

#include "acpi.hpp"

namespace arch::x86_64::apic {

enum class Status : uint8_t {
    Ok = 0,
    InvalidTopology,
    PagingUnavailable,
    MappingFailed,
    HardwareUnavailable,
    NotPrepared,
    CpuUnsupported,
    UnsupportedMode,
    BaseMismatch,
};

constexpr uint8_t SPURIOUS_VECTOR = 0xFFU;

// Maps and reads the APIC register blocks to verify the MADT topology. It does
// not enable APIC interrupt delivery: the legacy PIC remains the active,
// recoverable interrupt path until interrupt migration is implemented.
Status prepare(const acpi::Topology& topology);
// Enables only the current CPU's xAPIC unit. Legacy external IRQs continue to
// use the qualified PIC path until I/O APIC migration is implemented.
Status enable_local();
bool prepared();
bool local_enabled();
// Acknowledges a Local APIC-delivered interrupt. It is a no-op until the
// current CPU's Local APIC has been enabled.
void send_eoi();
uint32_t local_apic_id();
uint32_t local_apic_version();
size_t io_apic_count();
uint32_t io_apic_version(size_t index);
const char* status_message(Status status);

} // namespace arch::x86_64::apic

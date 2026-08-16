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
};

// Maps and reads the APIC register blocks to verify the MADT topology. It does
// not enable APIC interrupt delivery: the legacy PIC remains the active,
// recoverable interrupt path until interrupt migration is implemented.
Status prepare(const acpi::Topology& topology);
bool prepared();
uint32_t local_apic_id();
uint32_t local_apic_version();
size_t io_apic_count();
uint32_t io_apic_version(size_t index);
const char* status_message(Status status);

} // namespace arch::x86_64::apic

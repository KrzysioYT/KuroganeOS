#pragma once

#include <stddef.h>
#include <stdint.h>

#include "acpi.hpp"
#include "io_apic_route.hpp"

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
    IoRouteUnavailable,
    InvalidRoute,
    GsiOutOfRange,
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
size_t io_apic_redirection_count(size_t index);
// Programs one masked-then-unmasked fixed delivery entry. The route remains
// allocation-free and is rejected until a validated MADT topology is mapped.
Status route_gsi(uint32_t global_system_interrupt,
                 const io_apic::Route& route);
// Masks and clears one previously routed entry before vector reuse.
Status clear_gsi(uint32_t global_system_interrupt);
const char* status_message(Status status);

} // namespace arch::x86_64::apic

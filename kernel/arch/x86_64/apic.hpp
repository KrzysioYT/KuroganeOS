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
    InvalidLegacyIrq,
    GsiOutOfRange,
};

constexpr uint8_t SPURIOUS_VECTOR = 0xFFU;

Status prepare(const acpi::Topology& topology);
Status enable_local();
bool prepared();
bool local_enabled();
void send_eoi();
uint32_t local_apic_id();
uint32_t local_apic_version();
size_t io_apic_count();
uint32_t io_apic_version(size_t index);
size_t io_apic_redirection_count(size_t index);
Status route_gsi(uint32_t global_system_interrupt,
                 const io_apic::Route& route);
Status clear_gsi(uint32_t global_system_interrupt);
Status route_legacy_irq(uint8_t legacy_irq, const io_apic::Route& route);
const char* status_message(Status status);

} // namespace arch::x86_64::apic

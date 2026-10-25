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
    IpiTimeout,
    TimerUnavailable,
};

constexpr uint8_t SPURIOUS_VECTOR = 0xFFU;
constexpr uint8_t SCHEDULER_TIMER_VECTOR = 0xF2U;
constexpr uint32_t TIMER_DIVIDE_BY_16 = 0x3U;

Status prepare(const acpi::Topology& topology);
Status enable_local();
bool prepared();
bool local_enabled();
void send_eoi();
uint32_t local_apic_id();
uint32_t current_apic_id();
uint32_t local_apic_version();
Status send_ipi(uint32_t destination_apic_id, uint8_t vector);
Status send_init(uint32_t destination_apic_id);
Status send_startup(uint32_t destination_apic_id, uint8_t startup_vector);

// Local APIC timer primitives. Calibration is performed on the BSP and the
// resulting initial count can then be programmed independently on every AP.
Status timer_begin_calibration(uint32_t divide_config = TIMER_DIVIDE_BY_16);
uint32_t timer_current_count();
Status timer_start_periodic(
    uint8_t vector,
    uint32_t initial_count,
    uint32_t divide_config = TIMER_DIVIDE_BY_16);
void timer_stop();
size_t io_apic_count();
uint32_t io_apic_version(size_t index);
size_t io_apic_redirection_count(size_t index);
Status route_gsi(uint32_t global_system_interrupt,
                 const io_apic::Route& route);
Status clear_gsi(uint32_t global_system_interrupt);
Status route_legacy_irq(uint8_t legacy_irq, const io_apic::Route& route);
const char* status_message(Status status);

} // namespace arch::x86_64::apic

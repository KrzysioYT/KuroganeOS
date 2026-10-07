#pragma once

#include <stddef.h>
#include <stdint.h>

#include "acpi.hpp"

namespace arch::x86_64::smp {

constexpr uint8_t TLB_SHOOTDOWN_VECTOR = 0xF0U;
constexpr uint8_t WORK_VECTOR = 0xF1U;
constexpr size_t MAXIMUM_CPUS = acpi::MAXIMUM_PROCESSORS;

enum class Status : uint8_t {
    Ok = 0,
    AlreadyInitialized,
    InvalidTopology,
    ApicUnavailable,
    IdtUnavailable,
    PageTableUnavailable,
    TrampolineTooLarge,
    HandlerRegistrationFailed,
    ApStartupTimeout,
    WorkTimeout,
    ShootdownTimeout,
};

using WorkCallback = void (*)(size_t cpu_index, void* context);

Status initialize(const acpi::Topology& topology);
bool initialized();
size_t discovered_cpu_count();
size_t online_cpu_count();
size_t current_cpu_index();
uint32_t current_apic_id();

Status run_on_all_cpus(
    WorkCallback callback,
    void* context,
    uint32_t spin_budget = UINT32_C(20000000));

Status shootdown_page(
    uintptr_t virtual_address,
    uint32_t spin_budget = UINT32_C(20000000));

bool qualify_parallel_dispatch();
bool qualify_tlb_shootdown();

const char* status_message(Status status);

} // namespace arch::x86_64::smp

extern "C" [[noreturn]] void kurogane_smp_ap_entry(
    uint32_t cpu_index,
    uint32_t expected_apic_id);

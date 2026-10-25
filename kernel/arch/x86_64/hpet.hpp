#pragma once

#include <stddef.h>
#include <stdint.h>

#include "hpet_table.hpp"

namespace arch::x86_64::hpet {

enum class Status : uint8_t {
    Ok = 0,
    AlreadyInitialized,
    InvalidArgument,
    TableUnavailable,
    InvalidTable,
    PagingUnavailable,
    MappingFailed,
    InvalidCapabilities,
    CounterUnavailable,
};

Status initialize(uint64_t rsdp_physical_address);
void shutdown();

bool initialized();
uint64_t counter();
uint32_t counter_period_femtoseconds();
const Capabilities* capabilities();

// Bounded proof that the free-running main counter changes. This does not
// configure comparator interrupts and does not replace the PIT scheduler tick.
bool counter_advances(size_t read_budget);

const char* status_message(Status status);

} // namespace arch::x86_64::hpet

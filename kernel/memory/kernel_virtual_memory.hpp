#pragma once

#include <stdint.h>

#include "virtual_memory.hpp"

namespace memory::kernel_virtual_memory {

enum class Status : uint8_t {
    Ok = 0,
    PhysicalMemoryUnavailable,
    FiveLevelPagingUnsupported,
    InvalidRootTable,
    BackendInitializationFailed,
    NoTestVirtualAddress,
    TestFrameAllocationFailed,
    TestMappingFailed,
    TestTranslationFailed,
    TestDataMismatch,
    TestUnmapFailed,
    TestFrameLeak
};

// Clones the four-level root left active by x86-64 UEFI into a PMM-owned page
// and switches CR3 to that private root. Inherited lower tables and physical
// pages remain reachable through UEFI's identity mapping during this preview.
// This is a uniprocessor backend; SMP requires a real TLB-shootdown callback.
Status initialize();
Status self_test();

bool initialized();
uint64_t root_table_physical();
uint8_t physical_address_bits();
virtual_memory::AddressSpace* address_space();
const char* status_message(Status status);

} // namespace memory::kernel_virtual_memory

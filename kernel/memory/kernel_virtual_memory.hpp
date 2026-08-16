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
    TestFrameLeak,
    AddressSpaceAllocationFailed,
    AddressSpaceInitializationFailed,
    AddressSpaceActive
};

struct OwnedAddressSpace {
    virtual_memory::AddressSpace address_space;
    void* root_frame;
    bool initialized;
};

// Clones the four-level root left active by x86-64 UEFI into a PMM-owned page
// and switches CR3 to that private root. Inherited lower tables and physical
// pages remain reachable through UEFI's identity mapping during this preview.
// This is a uniprocessor backend; SMP requires a real TLB-shootdown callback.
Status initialize();
Status self_test();

// Creates a private PML4 by cloning the kernel root. Inherited kernel/firmware
// mappings remain supervisor-only; callers may add user mappings only below
// the canonical user limit. Every caller-created mapping must be removed
// before destroy_address_space so module-owned lower tables can be reclaimed.
Status create_address_space(OwnedAddressSpace* output);
Status activate(OwnedAddressSpace* address_space);
Status activate_kernel();
Status destroy_address_space(OwnedAddressSpace* address_space);

bool initialized();
uint64_t root_table_physical();
// Returns the physical PML4 currently loaded in CR3. This is used by the
// process runtime to resolve an interrupt to its owning address space.
uint64_t active_root_table_physical();
uint8_t physical_address_bits();
virtual_memory::AddressSpace* address_space();
bool is_active(const OwnedAddressSpace* address_space);
const char* status_message(Status status);

} // namespace memory::kernel_virtual_memory

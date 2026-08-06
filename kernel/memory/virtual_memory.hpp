#pragma once

#include <stddef.h>
#include <stdint.h>

namespace memory::virtual_memory {

constexpr uint64_t PAGE_SIZE = UINT64_C(4096);
constexpr size_t PAGE_TABLE_ENTRY_COUNT = 512;
constexpr uint8_t ARCHITECTURAL_PHYSICAL_ADDRESS_BITS = 52;

// Flags describe the effective permissions of a 4 KiB mapping. Presence is
// implicit: a successful query always describes a present mapping.
enum class MapFlags : uint64_t {
    None = 0,
    Writable = UINT64_C(1) << 0,
    User = UINT64_C(1) << 1,
    WriteThrough = UINT64_C(1) << 2,
    CacheDisable = UINT64_C(1) << 3,
    Global = UINT64_C(1) << 4,
    NoExecute = UINT64_C(1) << 5
};

constexpr MapFlags operator|(MapFlags left, MapFlags right) {
    return static_cast<MapFlags>(
        static_cast<uint64_t>(left) | static_cast<uint64_t>(right));
}

constexpr MapFlags operator&(MapFlags left, MapFlags right) {
    return static_cast<MapFlags>(
        static_cast<uint64_t>(left) & static_cast<uint64_t>(right));
}

inline MapFlags& operator|=(MapFlags& left, MapFlags right) {
    left = left | right;
    return left;
}

constexpr bool has_flag(MapFlags flags, MapFlags flag) {
    return (static_cast<uint64_t>(flags) & static_cast<uint64_t>(flag)) != 0;
}

enum class Status : uint8_t {
    Ok = 0,
    NotInitialized,
    InvalidArgument,
    InvalidFlags,
    NonCanonicalAddress,
    UnalignedAddress,
    AddressOverflow,
    PhysicalAddressOutOfRange,
    AlreadyMapped,
    NotMapped,
    OutOfMemory,
    BackendFailure,
    HugePageConflict,
    PermissionConflict,
    CorruptPageTable
};

using AllocateTableCallback = bool (*)(
    void* context,
    uint64_t* out_physical_address);
using FreeTableCallback = void (*)(
    void* context,
    uint64_t physical_address);
using PhysicalToVirtualCallback = void* (*)(
    void* context,
    uint64_t physical_address);
using InvalidatePageCallback = void (*)(
    void* context,
    uint64_t virtual_address);

struct Backend {
    void* context;

    // Allocation must return a page-aligned physical page that is exclusively
    // owned by the caller until free_table is invoked. Its previous contents
    // do not matter; the module clears the complete page before publishing it.
    AllocateTableCallback allocate_table;
    FreeTableCallback free_table;

    // The returned pointer must provide access to the complete 4 KiB physical
    // page and remain valid while that page belongs to the backend or caller.
    PhysicalToVirtualCallback physical_to_virtual;

    // This callback must synchronously invalidate the translation and relevant
    // paging-structure caches for the virtual page on every processor that can
    // use this address space. Detached page-table pages may be freed as soon as
    // the callback returns.
    InvalidatePageCallback invalidate_page;

    // Zero selects the architectural 52-bit maximum. A kernel backend should
    // set the CPUID-reported physical address width when it is smaller.
    uint8_t physical_address_bits;
};

struct AddressSpace {
    uint64_t root_table_physical;
    uint64_t physical_address_mask;
    Backend backend;
    bool initialized;
};

struct Mapping {
    uint64_t virtual_address;
    uint64_t physical_address;
    MapFlags flags;
    bool accessed;
    bool dirty;
};

// The caller owns the root PML4 page and must keep it alive. The module reserves
// one x86 available-to-software bit in non-leaf entries to identify lower-level
// tables allocated through this backend. External synchronization is required
// when an address space can be modified concurrently. initialize is
// transactional: an error leaves the supplied AddressSpace unchanged.
Status initialize(
    AddressSpace* address_space,
    uint64_t root_table_physical,
    const Backend* backend);

Status map_page(
    AddressSpace* address_space,
    uint64_t virtual_address,
    uint64_t physical_address,
    MapFlags flags);

Status unmap_page(
    AddressSpace* address_space,
    uint64_t virtual_address,
    Mapping* removed_mapping = nullptr);

Status query_page(
    const AddressSpace* address_space,
    uint64_t virtual_address,
    Mapping* mapping);

Status translate(
    const AddressSpace* address_space,
    uint64_t virtual_address,
    uint64_t* physical_address);

const char* status_message(Status status);

} // namespace memory::virtual_memory

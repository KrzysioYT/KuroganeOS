#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pci.hpp"
#include "../arch/x86_64/hardware_vectors.hpp"
#include "../arch/x86_64/interrupts.hpp"

namespace pci::msix {

constexpr uint16_t PCI_COMMAND_INTX_DISABLE = UINT16_C(1) << 10U;
constexpr uint16_t CONTROL_FUNCTION_MASK = UINT16_C(1) << 14U;
constexpr uint16_t CONTROL_ENABLE = UINT16_C(1) << 15U;
constexpr uint32_t VECTOR_MASK = UINT32_C(1);
constexpr size_t TABLE_ENTRY_BYTES = 16U;
constexpr size_t MAXIMUM_TABLE_ENTRIES = 2048U;

enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    LocalApicUnavailable,
    CapabilityMissing,
    CapabilityMalformed,
    AlreadyEnabled,
    RegionMismatch,
    TableOutOfRange,
    PendingArrayOutOfRange,
    EntryOutOfRange,
    VectorUnavailable,
    StaleRoute,
    NotActive,
};

struct ConfigAccess {
    uint16_t (*read16)(Address address, uint8_t offset, void* context);
    void (*write16)(
        Address address,
        uint8_t offset,
        uint16_t value,
        void* context);
    void* context;
};

struct TableAccess {
    uint32_t (*read32)(uint32_t byte_offset, void* context);
    void (*write32)(uint32_t byte_offset, uint32_t value, void* context);
    void* context;
};

// A driver supplies an already mapped complete BAR region. The MSI-X core
// validates table/PBA spans against it and never guesses a BAR size or maps
// unbounded device memory while an endpoint may be active.
struct MmioRegion {
    uint8_t bar_index;
    uint64_t physical_address;
    volatile void* virtual_address;
    size_t bytes;
};

struct ProgrammedState {
    Address address;
    uint8_t capability_offset;
    uint16_t table_size;
    uint16_t entry_index;
    uint32_t entry_offset;
    bool active;
    bool command_held;
    uint16_t original_command;
    uint16_t original_control;
    uint32_t original_address_low;
    uint32_t original_address_high;
    uint32_t original_data;
    uint32_t original_vector_control;
};

struct Route {
    arch::x86_64::hardware_vectors::Lease vector;
    ProgrammedState programmed;
    volatile uint8_t* table_base;
    size_t table_bytes;
    bool active;
};

Status validate_regions(
    const MsiXInfo& info,
    const MmioRegion& table_region,
    const MmioRegion& pending_region);

// Public transaction primitives make ordering and rollback deterministic in
// host tests. Drivers normally use enable_single()/disable().
Status program_single(
    Address address,
    uint8_t capability_offset,
    uint16_t table_size,
    uint16_t entry_index,
    uint32_t entry_offset,
    uint8_t vector,
    uint32_t destination_apic_id,
    const ConfigAccess& config,
    const TableAccess& table,
    ProgrammedState* output);
Status quiesce(
    const ConfigAccess& config,
    const TableAccess& table,
    ProgrammedState* state);
Status finish_restore(const ConfigAccess& config, ProgrammedState* state);

Status enable_single(
    const Device& device,
    uint16_t entry_index,
    const MmioRegion& table_region,
    const MmioRegion& pending_region,
    arch::x86_64::interrupts::InterruptHandler handler,
    Route* output);
Status disable(Route* route);

const char* status_name(Status status);

} // namespace pci::msix

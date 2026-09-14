#pragma once

#include "pci_msix.hpp"

namespace pci::msix {

// One owner controls the function-wide enable/mask bits and all selected
// entries. This bounded, allocation-free API does not provide SMP affinity or
// hot-unplug synchronization. The caller serializes the device lifecycle,
// keeps its interrupt sources stopped during enable_group(), and stops/drains
// them again before disable_group().
constexpr size_t MAX_GROUP_ROUTES = 8U;

struct Message {
    uint16_t entry_index;
    uint8_t vector;
    uint32_t destination_apic_id;
};

struct SavedEntry {
    uint32_t offset;
    uint32_t address_low;
    uint32_t address_high;
    uint32_t data;
    uint32_t vector_control;
};

struct GroupState {
    Address address;
    uint8_t capability_offset;
    uint16_t table_size;
    uint32_t table_offset;
    size_t count;
    SavedEntry entries[MAX_GROUP_ROUTES];
    uint16_t original_command;
    uint16_t original_control;
    bool active;
    bool command_held;
};

struct RouteRequest {
    uint16_t entry_index;
    arch::x86_64::interrupts::InterruptHandler handler;
};

struct RouteGroup {
    arch::x86_64::hardware_vectors::Lease vectors[MAX_GROUP_ROUTES];
    size_t count;
    GroupState programmed;
    volatile uint8_t* table_base;
    size_t table_bytes;
};

// Outputs must be zero-initialized or fully retired. A live output is rejected
// without overwriting ownership. Unselected table entries must already be
// masked; they are never modified or enabled on another owner's behalf.
Status program_group(
    Address address, uint8_t capability_offset, uint16_t table_size,
    uint32_t table_offset, const Message* messages, size_t count,
    const ConfigAccess& config, const TableAccess& table, GroupState* output);
Status quiesce_group(
    const ConfigAccess& config, const TableAccess& table, GroupState* state);
Status finish_group_restore(const ConfigAccess& config, GroupState* state);

Status enable_group(
    const Device& device, const RouteRequest* requests, size_t count,
    const MmioRegion& table_region, const MmioRegion& pending_region,
    RouteGroup* output);
Status disable_group(RouteGroup* group);

} // namespace pci::msix

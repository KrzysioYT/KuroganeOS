#include "pci_msix_group.hpp"

#include "pci_msi.hpp"
#include "../arch/x86_64/apic.hpp"

namespace pci::msix {
namespace {

bool valid_config(const ConfigAccess& config) {
    return config.read16 != nullptr && config.write16 != nullptr;
}

bool valid_table(const TableAccess& table) {
    return table.read32 != nullptr && table.write32 != nullptr;
}

bool valid_layout(uint8_t capability, uint16_t size, uint32_t offset) {
    return capability >= 0x40U && (capability & 3U) == 0U &&
        capability <= 0xF4U && size != 0U && size <= MAXIMUM_TABLE_ENTRIES &&
        (offset & 7U) == 0U &&
        static_cast<uint64_t>(offset) + size * TABLE_ENTRY_BYTES <=
            UINT64_C(0x100000000);
}

bool valid_state(const GroupState& state) {
    if (!valid_layout(state.capability_offset, state.table_size, state.table_offset) ||
        state.count == 0U || state.count > MAX_GROUP_ROUTES ||
        (state.original_control & CONTROL_ENABLE) != 0U ||
        (state.original_control & 0x7FFU) + 1U != state.table_size) return false;
    const uint64_t end = static_cast<uint64_t>(state.table_offset) +
        state.table_size * TABLE_ENTRY_BYTES;
    for (size_t i = 0U; i < state.count; ++i) {
        const uint32_t offset = state.entries[i].offset;
        if (offset < state.table_offset ||
            (offset - state.table_offset) % TABLE_ENTRY_BYTES != 0U ||
            static_cast<uint64_t>(offset) + TABLE_ENTRY_BYTES > end) return false;
        for (size_t j = 0U; j < i; ++j) {
            if (offset == state.entries[j].offset) return false;
        }
    }
    return true;
}

uint16_t read_config(Address address, uint8_t offset, void*) {
    return pci::read16(address, offset);
}

void write_config(Address address, uint8_t offset, uint16_t value, void*) {
    pci::write16(address, offset, value);
}

uint32_t read_table(uint32_t offset, void* context) {
    return *reinterpret_cast<volatile uint32_t*>(
        static_cast<volatile uint8_t*>(context) + offset);
}

void write_table(uint32_t offset, uint32_t value, void* context) {
    *reinterpret_cast<volatile uint32_t*>(
        static_cast<volatile uint8_t*>(context) + offset) = value;
    __asm__ volatile("mfence" : : : "memory");
}

ConfigAccess real_config() { return {read_config, write_config, nullptr}; }
TableAccess real_table(volatile uint8_t* base) {
    return {read_table, write_table, const_cast<uint8_t*>(base)};
}

// A posted MMIO write must reach the endpoint before releasing vector
// ownership or changing the function mask. A CPU fence alone is insufficient.
void drain_table(const TableAccess& table, uint32_t offset) {
    static_cast<void>(table.read32(offset + 12U, table.context));
}

} // namespace

Status program_group(
    Address address, uint8_t capability_offset, uint16_t table_size,
    uint32_t table_offset, const Message* messages, size_t count,
    const ConfigAccess& config, const TableAccess& table, GroupState* output) {
    if (output == nullptr || messages == nullptr || count == 0U ||
        count > MAX_GROUP_ROUTES || !valid_config(config) || !valid_table(table)) {
        return Status::InvalidArgument;
    }
    if (output->active || output->command_held) return Status::AlreadyEnabled;
    if (!valid_layout(capability_offset, table_size, table_offset)) {
        return Status::CapabilityMalformed;
    }
    for (size_t i = 0U; i < count; ++i) {
        if (messages[i].entry_index >= table_size) return Status::EntryOutOfRange;
        if (!arch::x86_64::hardware_vectors::is_allocatable(messages[i].vector) ||
            messages[i].destination_apic_id > 0xFFU) return Status::InvalidArgument;
        for (size_t j = 0U; j < i; ++j) {
            if (messages[i].entry_index == messages[j].entry_index ||
                messages[i].vector == messages[j].vector) return Status::InvalidArgument;
        }
    }
    const uint8_t control_offset = static_cast<uint8_t>(capability_offset + 2U);
    const uint16_t control = config.read16(address, control_offset, config.context);
    if ((control & CONTROL_ENABLE) != 0U) return Status::AlreadyEnabled;
    if ((control & 0x7FFU) + 1U != table_size) return Status::CapabilityMalformed;

    // Enabling the function must not expose an unowned, previously unmasked
    // entry. Reject that state before any device writes or ownership changes.
    for (uint16_t entry = 0U; entry < table_size; ++entry) {
        bool selected = false;
        for (size_t i = 0U; i < count; ++i) selected |= messages[i].entry_index == entry;
        if (!selected && (table.read32(table_offset + entry * 16U + 12U,
                table.context) & VECTOR_MASK) == 0U) return Status::UnmaskedEntry;
    }

    GroupState state{};
    state.address = address;
    state.capability_offset = capability_offset;
    state.table_size = table_size;
    state.table_offset = table_offset;
    state.count = count;
    state.original_command = config.read16(address, 4U, config.context);
    state.original_control = control;
    for (size_t i = 0U; i < count; ++i) {
        const uint32_t offset = table_offset + messages[i].entry_index * 16U;
        state.entries[i] = {offset,
            table.read32(offset, table.context), table.read32(offset + 4U, table.context),
            table.read32(offset + 8U, table.context), table.read32(offset + 12U, table.context)};
    }
    const uint16_t masked = static_cast<uint16_t>(control | CONTROL_FUNCTION_MASK);
    config.write16(address, control_offset, masked, config.context);
    for (size_t i = 0U; i < count; ++i) {
        const auto& entry = state.entries[i];
        table.write32(entry.offset + 12U, entry.vector_control | VECTOR_MASK, table.context);
    }
    for (size_t i = 0U; i < count; ++i) {
        const uint32_t offset = state.entries[i].offset;
        table.write32(offset, msi::message_address(
            static_cast<uint8_t>(messages[i].destination_apic_id)), table.context);
        table.write32(offset + 4U, 0U, table.context);
        table.write32(offset + 8U, msi::message_data(messages[i].vector), table.context);
    }
    drain_table(table, state.entries[count - 1U].offset);
    config.write16(address, 4U,
        static_cast<uint16_t>(state.original_command | PCI_COMMAND_INTX_DISABLE), config.context);
    config.write16(address, control_offset,
        static_cast<uint16_t>(masked | CONTROL_ENABLE), config.context);
    for (size_t i = 0U; i < count; ++i) {
        const auto& entry = state.entries[i];
        table.write32(entry.offset + 12U, entry.vector_control & ~VECTOR_MASK, table.context);
    }
    drain_table(table, state.entries[count - 1U].offset);
    config.write16(address, control_offset, static_cast<uint16_t>(
        (masked | CONTROL_ENABLE) & ~CONTROL_FUNCTION_MASK), config.context);
    state.active = true;
    state.command_held = true;
    *output = state;
    return Status::Ok;
}

Status quiesce_group(
    const ConfigAccess& config, const TableAccess& table, GroupState* state) {
    if (state == nullptr || !valid_config(config) || !valid_table(table)) {
        return Status::InvalidArgument;
    }
    if (!valid_state(*state)) return Status::CapabilityMalformed;
    if (!state->active || !state->command_held) return Status::NotActive;
    const uint8_t control_offset = static_cast<uint8_t>(state->capability_offset + 2U);
    const uint16_t control = config.read16(state->address, control_offset, config.context);
    config.write16(state->address, control_offset,
        static_cast<uint16_t>(control | CONTROL_FUNCTION_MASK), config.context);
    for (size_t i = 0U; i < state->count; ++i) {
        const uint32_t offset = state->entries[i].offset + 12U;
        table.write32(offset, table.read32(offset, table.context) | VECTOR_MASK, table.context);
    }
    drain_table(table, state->entries[state->count - 1U].offset);
    config.write16(state->address, control_offset, static_cast<uint16_t>(
        (control | CONTROL_FUNCTION_MASK) & ~CONTROL_ENABLE), config.context);
    for (size_t i = 0U; i < state->count; ++i) {
        const auto& entry = state->entries[i];
        table.write32(entry.offset, entry.address_low, table.context);
        table.write32(entry.offset + 4U, entry.address_high, table.context);
        table.write32(entry.offset + 8U, entry.data, table.context);
        table.write32(entry.offset + 12U, entry.vector_control, table.context);
    }
    drain_table(table, state->entries[state->count - 1U].offset);
    config.write16(state->address, control_offset, state->original_control, config.context);
    state->active = false;
    return Status::Ok;
}

Status finish_group_restore(const ConfigAccess& config, GroupState* state) {
    if (state == nullptr || !valid_config(config)) return Status::InvalidArgument;
    if (!valid_state(*state)) return Status::CapabilityMalformed;
    if (state->active || !state->command_held) return Status::NotActive;
    config.write16(state->address, 4U, state->original_command, config.context);
    state->command_held = false;
    return Status::Ok;
}

Status enable_group(
    const Device& device, const RouteRequest* requests, size_t count,
    const MmioRegion& table_region, const MmioRegion& pending_region,
    RouteGroup* output) {
    if (output == nullptr || requests == nullptr || count == 0U || count > MAX_GROUP_ROUTES) {
        return Status::InvalidArgument;
    }
    if (output->count != 0U || output->programmed.active || output->programmed.command_held) {
        return Status::AlreadyEnabled;
    }
    for (size_t i = 0U; i < count; ++i) {
        if (requests[i].handler == nullptr) return Status::InvalidArgument;
        for (size_t j = 0U; j < i; ++j) {
            if (requests[i].entry_index == requests[j].entry_index) return Status::InvalidArgument;
        }
    }
    if (!arch::x86_64::apic::local_enabled()) return Status::LocalApicUnavailable;
    Capability capability{};
    if (!find_capability(device, CapabilityId::MsiX, &capability)) return Status::CapabilityMissing;
    MsiXInfo info{};
    if (!read_msix_info(device, &info)) return Status::CapabilityMalformed;
    const Status region_status = validate_regions(info, table_region, pending_region);
    if (region_status != Status::Ok) return region_status;
    const uint8_t header = device.header_type & 0x7FU;
    const uint8_t bar_count = header == 0U ? 6U : (header == 1U ? 2U : 0U);
    if (info.table_bar >= bar_count || info.pending_bit_array_bar >= bar_count) {
        return Status::RegionMismatch;
    }
    bool table_io = false;
    bool pending_io = false;
    if (bar_address(device, info.table_bar, &table_io) != table_region.physical_address ||
        bar_address(device, info.pending_bit_array_bar, &pending_io) != pending_region.physical_address ||
        table_io || pending_io) return Status::RegionMismatch;
    for (size_t i = 0U; i < count; ++i) {
        if (requests[i].entry_index >= info.table_size) return Status::EntryOutOfRange;
    }

    RouteGroup group{};
    Message messages[MAX_GROUP_ROUTES]{};
    Status status = Status::Ok;
    for (size_t i = 0U; i < count; ++i) {
        if (arch::x86_64::interrupts::allocate_hardware_vector(
                requests[i].handler, &group.vectors[i]) !=
                arch::x86_64::hardware_vectors::Status::Ok) {
            status = Status::VectorUnavailable;
            break;
        }
        ++group.count;
        messages[i] = {requests[i].entry_index, group.vectors[i].vector,
            arch::x86_64::apic::local_apic_id()};
    }
    if (status == Status::Ok) {
        group.table_base = static_cast<volatile uint8_t*>(table_region.virtual_address);
        group.table_bytes = table_region.bytes;
        status = program_group(device.address, info.offset, info.table_size,
            info.table_offset, messages, count, real_config(), real_table(group.table_base),
            &group.programmed);
    }
    if (status != Status::Ok) {
        // No endpoint writes occur on a rejected transaction. Preserve any
        // unreleased lease so a caller cannot silently lose cleanup ownership.
        bool released = true;
        for (size_t i = 0U; i < group.count; ++i) {
            if (arch::x86_64::interrupts::release_hardware_vector(group.vectors[i]) ==
                    arch::x86_64::hardware_vectors::Status::Ok) group.vectors[i] = {};
            else released = false;
        }
        if (!released) { *output = group; return Status::StaleRoute; }
        return status;
    }
    *output = group;
    return Status::Ok;
}

Status disable_group(RouteGroup* group) {
    if (group == nullptr) return Status::InvalidArgument;
    if (group->count == 0U) return Status::NotActive;
    if (group->count > MAX_GROUP_ROUTES) return Status::InvalidArgument;
    // Validate every lease before touching any register. A stale copy must not
    // mask a function or free a vector now belonging to a different owner.
    for (size_t i = 0U; i < group->count; ++i) {
        if (group->vectors[i].generation == 0U) {
            if (group->programmed.active) return Status::StaleRoute;
            continue;
        }
        if (!arch::x86_64::interrupts::owns_hardware_vector(group->vectors[i])) {
            return Status::StaleRoute;
        }
        for (size_t j = 0U; j < i; ++j) {
            if (group->vectors[i].vector == group->vectors[j].vector) return Status::StaleRoute;
        }
    }
    if (group->programmed.active || group->programmed.command_held) {
        if (!valid_state(group->programmed) || group->programmed.count != group->count ||
            group->table_base == nullptr ||
            static_cast<uint64_t>(group->programmed.table_offset) +
                group->programmed.table_size * TABLE_ENTRY_BYTES > group->table_bytes) {
            return Status::TableOutOfRange;
        }
    }
    if (group->programmed.active) {
        const Status status = quiesce_group(real_config(), real_table(group->table_base),
            &group->programmed);
        if (status != Status::Ok) return status;
    }
    bool released = true;
    for (size_t i = 0U; i < group->count; ++i) {
        if (group->vectors[i].generation == 0U) continue;
        if (arch::x86_64::interrupts::release_hardware_vector(group->vectors[i]) ==
                arch::x86_64::hardware_vectors::Status::Ok) group->vectors[i] = {};
        else released = false;
    }
    if (!released) return Status::StaleRoute;
    if (group->programmed.command_held) {
        const Status status = finish_group_restore(real_config(), &group->programmed);
        if (status != Status::Ok) return status;
    }
    *group = {};
    return Status::Ok;
}

} // namespace pci::msix

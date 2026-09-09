#include "pci_msix.hpp"

#include "pci_msi.hpp"
#include "../arch/x86_64/apic.hpp"

namespace pci::msix {
namespace {

constexpr uint16_t kTableSizeMask = UINT16_C(0x07FF);

bool valid_config(const ConfigAccess& access) {
    return access.read16 != nullptr && access.write16 != nullptr;
}

bool valid_table(const TableAccess& access) {
    return access.read32 != nullptr && access.write32 != nullptr;
}

bool config_range_valid(uint16_t offset, uint16_t width) {
    return offset < UINT16_C(0x100) &&
        width <= static_cast<uint16_t>(UINT16_C(0x100) - offset);
}

bool add_fits(size_t base, size_t length, size_t capacity) {
    return base <= capacity && length <= capacity - base;
}

bool spans_overlap(size_t first, size_t first_size, size_t second, size_t second_size) {
    return first < second + second_size && second < first + first_size;
}

bool valid_state(const ProgrammedState& state) {
    return state.capability_offset >= 0x40U &&
        (state.capability_offset & 0x03U) == 0U &&
        config_range_valid(state.capability_offset, 4U) &&
        state.table_size != 0U &&
        state.table_size <= MAXIMUM_TABLE_ENTRIES &&
        state.entry_index < state.table_size &&
        (state.entry_offset & UINT32_C(3)) == 0U &&
        state.entry_offset <= UINT32_MAX - 12U;
}

uint16_t real_read16(Address address, uint8_t offset, void*) {
    return pci::read16(address, offset);
}

void real_write16(Address address, uint8_t offset, uint16_t value, void*) {
    pci::write16(address, offset, value);
}

uint32_t real_table_read32(uint32_t byte_offset, void* context) {
    const auto* base = static_cast<volatile uint8_t*>(context);
    return *reinterpret_cast<volatile const uint32_t*>(base + byte_offset);
}

void real_table_write32(uint32_t byte_offset, uint32_t value, void* context) {
    auto* base = static_cast<volatile uint8_t*>(context);
    *reinterpret_cast<volatile uint32_t*>(base + byte_offset) = value;
    __asm__ volatile("mfence" : : : "memory");
}

ConfigAccess real_config_access() {
    return {real_read16, real_write16, nullptr};
}

TableAccess real_table_access(volatile uint8_t* base) {
    return {real_table_read32, real_table_write32,
        const_cast<uint8_t*>(base)};
}

size_t header_bar_count(const Device& device) {
    switch (device.header_type & 0x7FU) {
        case 0x00U: return 6U;
        case 0x01U: return 2U;
        default: return 0U;
    }
}

} // namespace

Status validate_regions(
    const MsiXInfo& info,
    const MmioRegion& table_region,
    const MmioRegion& pending_region) {
    if (info.table_size == 0U || info.table_size > MAXIMUM_TABLE_ENTRIES ||
        table_region.virtual_address == nullptr ||
        pending_region.virtual_address == nullptr ||
        table_region.physical_address == 0U ||
        pending_region.physical_address == 0U ||
        table_region.bytes == 0U || pending_region.bytes == 0U ||
        (reinterpret_cast<uintptr_t>(table_region.virtual_address) & 3U) != 0U ||
        (reinterpret_cast<uintptr_t>(pending_region.virtual_address) & 3U) != 0U) {
        return Status::InvalidArgument;
    }
    if (info.table_bar > 5U || info.pending_bit_array_bar > 5U ||
        table_region.bar_index != info.table_bar ||
        pending_region.bar_index != info.pending_bit_array_bar) {
        return Status::RegionMismatch;
    }

    const size_t table_offset = static_cast<size_t>(info.table_offset);
    const size_t table_bytes =
        static_cast<size_t>(info.table_size) * TABLE_ENTRY_BYTES;
    const size_t pending_offset =
        static_cast<size_t>(info.pending_bit_array_offset);
    const size_t pending_bytes =
        ((static_cast<size_t>(info.table_size) + 63U) / 64U) * 8U;
    if (!add_fits(table_offset, table_bytes, table_region.bytes)) {
        return Status::TableOutOfRange;
    }
    if (!add_fits(pending_offset, pending_bytes, pending_region.bytes)) {
        return Status::PendingArrayOutOfRange;
    }
    if (info.table_bar == info.pending_bit_array_bar) {
        if (table_region.physical_address != pending_region.physical_address ||
            table_region.virtual_address != pending_region.virtual_address ||
            table_region.bytes != pending_region.bytes) {
            return Status::RegionMismatch;
        }
        if (spans_overlap(
                table_offset, table_bytes, pending_offset, pending_bytes)) {
            return Status::CapabilityMalformed;
        }
    }
    return Status::Ok;
}

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
    ProgrammedState* output) {
    if (output == nullptr || !valid_config(config) || !valid_table(table) ||
        !arch::x86_64::hardware_vectors::is_allocatable(vector) ||
        destination_apic_id > UINT32_C(0xFF) || table_size == 0U ||
        table_size > MAXIMUM_TABLE_ENTRIES || entry_index >= table_size ||
        (entry_offset & UINT32_C(3)) != 0U ||
        entry_offset > UINT32_MAX - 12U) {
        return Status::InvalidArgument;
    }
    *output = {};
    if (capability_offset < 0x40U || (capability_offset & 0x03U) != 0U ||
        !config_range_valid(capability_offset, 4U)) {
        return Status::CapabilityMalformed;
    }

    const uint8_t control_offset =
        static_cast<uint8_t>(capability_offset + 2U);
    const uint16_t control = config.read16(
        address, control_offset, config.context);
    if ((control & CONTROL_ENABLE) != 0U) return Status::AlreadyEnabled;
    const uint16_t advertised_size =
        static_cast<uint16_t>((control & kTableSizeMask) + 1U);
    if (advertised_size != table_size) return Status::CapabilityMalformed;

    ProgrammedState state{};
    state.address = address;
    state.capability_offset = capability_offset;
    state.table_size = table_size;
    state.entry_index = entry_index;
    state.entry_offset = entry_offset;
    state.original_command = config.read16(address, 0x04U, config.context);
    state.original_control = control;
    state.original_address_low = table.read32(entry_offset, table.context);
    state.original_address_high = table.read32(entry_offset + 4U, table.context);
    state.original_data = table.read32(entry_offset + 8U, table.context);
    state.original_vector_control =
        table.read32(entry_offset + 12U, table.context);

    const uint16_t masked_disabled = static_cast<uint16_t>(
        (control | CONTROL_FUNCTION_MASK) &
        static_cast<uint16_t>(~CONTROL_ENABLE));
    config.write16(address, control_offset, masked_disabled, config.context);
    table.write32(
        entry_offset + 12U,
        state.original_vector_control | VECTOR_MASK,
        table.context);
    table.write32(
        entry_offset,
        pci::msi::message_address(static_cast<uint8_t>(destination_apic_id)),
        table.context);
    table.write32(entry_offset + 4U, 0U, table.context);
    table.write32(
        entry_offset + 8U,
        static_cast<uint32_t>(pci::msi::message_data(vector)),
        table.context);
    config.write16(
        address,
        0x04U,
        static_cast<uint16_t>(
            state.original_command | PCI_COMMAND_INTX_DISABLE),
        config.context);
    config.write16(
        address,
        control_offset,
        static_cast<uint16_t>(masked_disabled | CONTROL_ENABLE),
        config.context);
    table.write32(
        entry_offset + 12U,
        state.original_vector_control & ~VECTOR_MASK,
        table.context);
    config.write16(
        address,
        control_offset,
        static_cast<uint16_t>(
            (masked_disabled | CONTROL_ENABLE) &
            static_cast<uint16_t>(~CONTROL_FUNCTION_MASK)),
        config.context);

    state.active = true;
    state.command_held = true;
    *output = state;
    return Status::Ok;
}

Status quiesce(
    const ConfigAccess& config,
    const TableAccess& table,
    ProgrammedState* state) {
    if (state == nullptr || !valid_config(config) || !valid_table(table)) {
        return Status::InvalidArgument;
    }
    if (!valid_state(*state)) return Status::CapabilityMalformed;
    if (!state->active) return Status::NotActive;

    const uint8_t control_offset =
        static_cast<uint8_t>(state->capability_offset + 2U);
    const uint16_t control = config.read16(
        state->address, control_offset, config.context);
    config.write16(
        state->address,
        control_offset,
        static_cast<uint16_t>(control | CONTROL_FUNCTION_MASK),
        config.context);
    const uint32_t vector_control = table.read32(
        state->entry_offset + 12U, table.context);
    table.write32(
        state->entry_offset + 12U,
        vector_control | VECTOR_MASK,
        table.context);
    config.write16(
        state->address,
        control_offset,
        static_cast<uint16_t>(
            (control | CONTROL_FUNCTION_MASK) &
            static_cast<uint16_t>(~CONTROL_ENABLE)),
        config.context);

    table.write32(
        state->entry_offset, state->original_address_low, table.context);
    table.write32(
        state->entry_offset + 4U,
        state->original_address_high,
        table.context);
    table.write32(
        state->entry_offset + 8U, state->original_data, table.context);
    table.write32(
        state->entry_offset + 12U,
        state->original_vector_control,
        table.context);
    config.write16(
        state->address,
        control_offset,
        state->original_control,
        config.context);
    state->active = false;
    return Status::Ok;
}

Status finish_restore(const ConfigAccess& config, ProgrammedState* state) {
    if (state == nullptr || !valid_config(config)) {
        return Status::InvalidArgument;
    }
    if (!valid_state(*state)) return Status::CapabilityMalformed;
    if (state->active || !state->command_held) return Status::NotActive;
    config.write16(
        state->address, 0x04U, state->original_command, config.context);
    state->command_held = false;
    return Status::Ok;
}

Status enable_single(
    const Device& device,
    uint16_t entry_index,
    const MmioRegion& table_region,
    const MmioRegion& pending_region,
    arch::x86_64::interrupts::InterruptHandler handler,
    Route* output) {
    if (handler == nullptr || output == nullptr) return Status::InvalidArgument;
    *output = {};
    if (!arch::x86_64::apic::local_enabled()) {
        return Status::LocalApicUnavailable;
    }
    Capability capability{};
    if (!find_capability(device, CapabilityId::MsiX, &capability)) {
        return Status::CapabilityMissing;
    }
    MsiXInfo info{};
    if (!read_msix_info(device, &info)) return Status::CapabilityMalformed;
    const Status region_status =
        validate_regions(info, table_region, pending_region);
    if (region_status != Status::Ok) return region_status;
    const size_t bar_count = header_bar_count(device);
    if (info.table_bar >= bar_count || info.pending_bit_array_bar >= bar_count) {
        return Status::RegionMismatch;
    }
    bool table_is_io = false;
    bool pending_is_io = false;
    const uint64_t table_physical =
        pci::bar_address(device, info.table_bar, &table_is_io);
    const uint64_t pending_physical =
        pci::bar_address(device, info.pending_bit_array_bar, &pending_is_io);
    if (table_is_io || pending_is_io || table_physical == 0U ||
        pending_physical == 0U ||
        table_physical != table_region.physical_address ||
        pending_physical != pending_region.physical_address) {
        return Status::RegionMismatch;
    }
    if (entry_index >= info.table_size) return Status::EntryOutOfRange;
    const size_t entry_offset = static_cast<size_t>(info.table_offset) +
        static_cast<size_t>(entry_index) * TABLE_ENTRY_BYTES;
    if (entry_offset > UINT32_MAX ||
        !add_fits(entry_offset, TABLE_ENTRY_BYTES, table_region.bytes)) {
        return Status::EntryOutOfRange;
    }

    arch::x86_64::hardware_vectors::Lease vector{};
    const auto vector_status =
        arch::x86_64::interrupts::allocate_hardware_vector(handler, &vector);
    if (vector_status != arch::x86_64::hardware_vectors::Status::Ok) {
        return Status::VectorUnavailable;
    }

    auto* table_base = static_cast<volatile uint8_t*>(
        table_region.virtual_address);
    ProgrammedState programmed{};
    const Status program_status = program_single(
        device.address,
        info.offset,
        info.table_size,
        entry_index,
        static_cast<uint32_t>(entry_offset),
        vector.vector,
        arch::x86_64::apic::local_apic_id(),
        real_config_access(),
        real_table_access(table_base),
        &programmed);
    if (program_status != Status::Ok) {
        static_cast<void>(
            arch::x86_64::interrupts::release_hardware_vector(vector));
        return program_status;
    }
    output->vector = vector;
    output->programmed = programmed;
    output->table_base = table_base;
    output->table_bytes = table_region.bytes;
    output->active = true;
    return Status::Ok;
}

Status disable(Route* route) {
    if (route == nullptr) return Status::InvalidArgument;
    if (!route->active || route->table_base == nullptr) return Status::NotActive;
    if (!arch::x86_64::interrupts::owns_hardware_vector(route->vector)) {
        return Status::StaleRoute;
    }
    if (!add_fits(
            route->programmed.entry_offset,
            TABLE_ENTRY_BYTES,
            route->table_bytes)) {
        return Status::EntryOutOfRange;
    }

    const ConfigAccess config = real_config_access();
    const TableAccess table = real_table_access(route->table_base);
    const Status quiesce_status = quiesce(config, table, &route->programmed);
    if (quiesce_status != Status::Ok) return quiesce_status;
    const auto release_status =
        arch::x86_64::interrupts::release_hardware_vector(route->vector);
    const Status restore_status = finish_restore(config, &route->programmed);
    route->active = false;
    route->table_base = nullptr;
    route->table_bytes = 0U;
    if (restore_status != Status::Ok) return restore_status;
    if (release_status != arch::x86_64::hardware_vectors::Status::Ok) {
        return Status::StaleRoute;
    }
    return Status::Ok;
}

const char* status_name(Status status) {
    switch (status) {
        case Status::Ok: return "OK";
        case Status::InvalidArgument: return "INVALID_ARGUMENT";
        case Status::LocalApicUnavailable: return "LOCAL_APIC_UNAVAILABLE";
        case Status::CapabilityMissing: return "CAPABILITY_MISSING";
        case Status::CapabilityMalformed: return "CAPABILITY_MALFORMED";
        case Status::AlreadyEnabled: return "ALREADY_ENABLED";
        case Status::RegionMismatch: return "REGION_MISMATCH";
        case Status::TableOutOfRange: return "TABLE_OUT_OF_RANGE";
        case Status::PendingArrayOutOfRange: return "PBA_OUT_OF_RANGE";
        case Status::EntryOutOfRange: return "ENTRY_OUT_OF_RANGE";
        case Status::VectorUnavailable: return "VECTOR_UNAVAILABLE";
        case Status::StaleRoute: return "STALE_ROUTE";
        case Status::NotActive: return "NOT_ACTIVE";
    }
    return "UNKNOWN";
}

} // namespace pci::msix

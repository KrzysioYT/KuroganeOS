#include "pci_msi.hpp"

#include "../arch/x86_64/apic.hpp"

namespace pci::msi {
namespace {

constexpr uint16_t kMsiEnable = UINT16_C(1);
constexpr uint16_t kMultipleMessageEnableMask = UINT16_C(0x0070);
constexpr uint16_t kAddress64Bit = UINT16_C(1) << 7U;
constexpr uint16_t kPerVectorMasking = UINT16_C(1) << 8U;

bool valid_access(const ConfigAccess& access) {
    return access.read16 != nullptr && access.read32 != nullptr &&
        access.write16 != nullptr && access.write32 != nullptr;
}

bool range_valid(uint16_t offset, uint16_t width) {
    return offset + width <= UINT16_C(0x100);
}

uint16_t data_offset(uint8_t capability_offset, bool address_64_bit) {
    return static_cast<uint16_t>(
        static_cast<uint16_t>(capability_offset) +
        (address_64_bit ? UINT16_C(12) : UINT16_C(8)));
}

uint16_t mask_offset(uint8_t capability_offset, bool address_64_bit) {
    return static_cast<uint16_t>(
        static_cast<uint16_t>(capability_offset) +
        (address_64_bit ? UINT16_C(16) : UINT16_C(12)));
}

uint16_t real_read16(Address address, uint8_t offset, void*) {
    return pci::read16(address, offset);
}

uint32_t real_read32(Address address, uint8_t offset, void*) {
    return pci::read32(address, offset);
}

void real_write16(Address address, uint8_t offset, uint16_t value, void*) {
    pci::write16(address, offset, value);
}

void real_write32(Address address, uint8_t offset, uint32_t value, void*) {
    pci::write32(address, offset, value);
}

ConfigAccess real_access() {
    return {real_read16, real_read32, real_write16, real_write32, nullptr};
}

bool valid_programmed_state(const ProgrammedState& state) {
    if (state.capability_offset < 0x40U ||
        (state.capability_offset & 0x03U) != 0U) {
        return false;
    }
    const uint16_t address_low_offset =
        static_cast<uint16_t>(state.capability_offset) + UINT16_C(4);
    const uint16_t address_high_offset =
        static_cast<uint16_t>(state.capability_offset) + UINT16_C(8);
    const uint16_t message_data_offset =
        data_offset(state.capability_offset, state.address_64_bit);
    const uint16_t vector_mask_offset =
        mask_offset(state.capability_offset, state.address_64_bit);
    return range_valid(address_low_offset, 4U) &&
        (!state.address_64_bit || range_valid(address_high_offset, 4U)) &&
        range_valid(message_data_offset, 2U) &&
        (!state.per_vector_masking || range_valid(vector_mask_offset, 8U));
}

} // namespace

uint32_t message_address(uint8_t destination_apic_id) {
    return MESSAGE_ADDRESS_BASE |
        (static_cast<uint32_t>(destination_apic_id) << 12U);
}

uint16_t message_data(uint8_t vector) {
    return static_cast<uint16_t>(vector);
}

Status program_single(
    Address address,
    uint8_t capability_offset,
    uint8_t vector,
    uint32_t destination_apic_id,
    const ConfigAccess& access,
    ProgrammedState* output) {
    if (output == nullptr || !valid_access(access) ||
        !arch::x86_64::hardware_vectors::is_allocatable(vector) ||
        destination_apic_id > UINT32_C(0xFF)) {
        return Status::InvalidArgument;
    }
    *output = {};
    if (capability_offset < 0x40U || (capability_offset & 0x03U) != 0U ||
        !range_valid(capability_offset, 4U)) {
        return Status::CapabilityMalformed;
    }

    const uint8_t control_offset =
        static_cast<uint8_t>(capability_offset + 2U);
    const uint16_t control = access.read16(
        address, control_offset, access.context);
    if ((control & kMsiEnable) != 0U) return Status::AlreadyEnabled;

    const bool address_64_bit = (control & kAddress64Bit) != 0U;
    const bool per_vector_masking = (control & kPerVectorMasking) != 0U;
    const uint16_t message_data_offset_wide =
        data_offset(capability_offset, address_64_bit);
    const uint16_t address_low_offset =
        static_cast<uint16_t>(capability_offset) + UINT16_C(4);
    const uint16_t address_high_offset =
        static_cast<uint16_t>(capability_offset) + UINT16_C(8);
    if (!range_valid(address_low_offset, 4U) ||
        (address_64_bit &&
         !range_valid(address_high_offset, 4U)) ||
        !range_valid(message_data_offset_wide, 2U)) {
        return Status::CapabilityMalformed;
    }
    const uint16_t vector_mask_offset_wide =
        mask_offset(capability_offset, address_64_bit);
    if (per_vector_masking && !range_valid(vector_mask_offset_wide, 8U)) {
        return Status::CapabilityMalformed;
    }
    const uint8_t message_data_offset =
        static_cast<uint8_t>(message_data_offset_wide);
    const uint8_t vector_mask_offset =
        static_cast<uint8_t>(vector_mask_offset_wide);

    ProgrammedState state{};
    state.address = address;
    state.capability_offset = capability_offset;
    state.address_64_bit = address_64_bit;
    state.per_vector_masking = per_vector_masking;
    state.original_command = access.read16(address, 0x04U, access.context);
    state.original_control = control;
    state.original_address_low = access.read32(
        address, static_cast<uint8_t>(capability_offset + 4U), access.context);
    if (address_64_bit) {
        state.original_address_high = access.read32(
            address,
            static_cast<uint8_t>(capability_offset + 8U),
            access.context);
    }
    state.original_data = access.read16(
        address, message_data_offset, access.context);
    if (per_vector_masking) {
        state.original_mask = access.read32(
            address, vector_mask_offset, access.context);
        access.write32(
            address,
            vector_mask_offset,
            state.original_mask | UINT32_C(1),
            access.context);
    }

    const uint16_t disabled_control = static_cast<uint16_t>(
        control & static_cast<uint16_t>(~(kMsiEnable | kMultipleMessageEnableMask)));
    access.write16(address, control_offset, disabled_control, access.context);
    access.write32(
        address,
        static_cast<uint8_t>(capability_offset + 4U),
        message_address(static_cast<uint8_t>(destination_apic_id)),
        access.context);
    if (address_64_bit) {
        access.write32(
            address,
            static_cast<uint8_t>(capability_offset + 8U),
            0U,
            access.context);
    }
    access.write16(
        address,
        message_data_offset,
        message_data(vector),
        access.context);
    access.write16(
        address,
        0x04U,
        static_cast<uint16_t>(state.original_command | PCI_COMMAND_INTX_DISABLE),
        access.context);
    access.write16(
        address,
        control_offset,
        static_cast<uint16_t>(disabled_control | kMsiEnable),
        access.context);
    if (per_vector_masking) {
        access.write32(
            address,
            vector_mask_offset,
            state.original_mask & ~UINT32_C(1),
            access.context);
    }
    state.active = true;
    state.command_held = true;
    *output = state;
    return Status::Ok;
}

Status quiesce(const ConfigAccess& access, ProgrammedState* state) {
    if (state == nullptr || !valid_access(access)) {
        return Status::InvalidArgument;
    }
    if (!valid_programmed_state(*state)) return Status::CapabilityMalformed;
    if (!state->active) return Status::NotActive;

    const uint8_t control_offset =
        static_cast<uint8_t>(state->capability_offset + 2U);
    const uint8_t message_data_offset = static_cast<uint8_t>(
        data_offset(state->capability_offset, state->address_64_bit));
    const uint8_t vector_mask_offset = static_cast<uint8_t>(
        mask_offset(state->capability_offset, state->address_64_bit));
    if (state->per_vector_masking) {
        const uint32_t mask = access.read32(
            state->address, vector_mask_offset, access.context);
        access.write32(
            state->address,
            vector_mask_offset,
            mask | UINT32_C(1),
            access.context);
    }
    const uint16_t control = access.read16(
        state->address, control_offset, access.context);
    access.write16(
        state->address,
        control_offset,
        static_cast<uint16_t>(control & static_cast<uint16_t>(~kMsiEnable)),
        access.context);

    access.write32(
        state->address,
        static_cast<uint8_t>(state->capability_offset + 4U),
        state->original_address_low,
        access.context);
    if (state->address_64_bit) {
        access.write32(
            state->address,
            static_cast<uint8_t>(state->capability_offset + 8U),
            state->original_address_high,
            access.context);
    }
    access.write16(
        state->address,
        message_data_offset,
        state->original_data,
        access.context);
    if (state->per_vector_masking) {
        access.write32(
            state->address,
            vector_mask_offset,
            state->original_mask,
            access.context);
    }
    access.write16(
        state->address,
        control_offset,
        state->original_control,
        access.context);
    state->active = false;
    return Status::Ok;
}

Status finish_restore(const ConfigAccess& access, ProgrammedState* state) {
    if (state == nullptr || !valid_access(access)) {
        return Status::InvalidArgument;
    }
    if (!valid_programmed_state(*state)) return Status::CapabilityMalformed;
    if (state->active || !state->command_held) return Status::NotActive;
    access.write16(
        state->address, 0x04U, state->original_command, access.context);
    state->command_held = false;
    return Status::Ok;
}

Status enable(
    const Device& device,
    arch::x86_64::interrupts::InterruptHandler handler,
    Route* output) {
    if (handler == nullptr || output == nullptr) return Status::InvalidArgument;
    *output = {};
    if (!arch::x86_64::apic::local_enabled()) {
        return Status::LocalApicUnavailable;
    }

    Capability capability{};
    if (!find_capability(device, CapabilityId::Msi, &capability)) {
        return Status::CapabilityMissing;
    }

    arch::x86_64::hardware_vectors::Lease vector{};
    const auto vector_status =
        arch::x86_64::interrupts::allocate_hardware_vector(handler, &vector);
    if (vector_status != arch::x86_64::hardware_vectors::Status::Ok) {
        return Status::VectorUnavailable;
    }

    ProgrammedState programmed{};
    const ConfigAccess access = real_access();
    const Status status = program_single(
        device.address,
        capability.offset,
        vector.vector,
        arch::x86_64::apic::local_apic_id(),
        access,
        &programmed);
    if (status != Status::Ok) {
        static_cast<void>(
            arch::x86_64::interrupts::release_hardware_vector(vector));
        return status;
    }

    output->vector = vector;
    output->programmed = programmed;
    output->active = true;
    return Status::Ok;
}

Status disable(Route* route) {
    if (route == nullptr) return Status::InvalidArgument;
    if (!route->active) return Status::NotActive;
    if (!arch::x86_64::interrupts::owns_hardware_vector(route->vector)) {
        return Status::StaleRoute;
    }

    const ConfigAccess access = real_access();
    const Status quiesce_status = quiesce(access, &route->programmed);
    if (quiesce_status != Status::Ok) return quiesce_status;

    const auto release_status =
        arch::x86_64::interrupts::release_hardware_vector(route->vector);
    const Status restore_status = finish_restore(access, &route->programmed);
    route->active = false;
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
        case Status::VectorUnavailable: return "VECTOR_UNAVAILABLE";
        case Status::StaleRoute: return "STALE_ROUTE";
        case Status::NotActive: return "NOT_ACTIVE";
    }
    return "UNKNOWN";
}

} // namespace pci::msi

#include "pci_power.hpp"

namespace pci::power {
namespace {

constexpr uint16_t kStateMask = UINT16_C(0x0003);
constexpr uint16_t kPmeEnable = UINT16_C(1) << 8U;
constexpr uint16_t kPmeStatus = UINT16_C(1) << 15U;
constexpr uint16_t kPmeClock = UINT16_C(1) << 3U;
constexpr uint16_t kD1Support = UINT16_C(1) << 9U;
constexpr uint16_t kD2Support = UINT16_C(1) << 10U;
constexpr uint16_t kPmeSupportMask = UINT16_C(0xF800);
constexpr uint8_t kPmeSupportShift = 11U;

uint16_t production_read16(Address address, uint8_t offset, void*) {
    return pci::read16(address, offset);
}

void production_write16(
    Address address,
    uint8_t offset,
    uint16_t value,
    void*) {
    pci::write16(address, offset, value);
}

const ConfigAccess kProductionAccess{
    production_read16,
    production_write16,
    nullptr,
};

bool valid_access(const ConfigAccess& access) {
    return access.read16 != nullptr && access.write16 != nullptr;
}

bool state_supported(const Info& info, State state) {
    switch (state) {
        case State::D0:
        case State::D3Hot:
            return true;
        case State::D1:
            return info.d1_supported;
        case State::D2:
            return info.d2_supported;
    }
    return false;
}

} // namespace

bool capability_layout_valid(uint8_t offset) {
    return offset >= 0x40U && offset <= 0xF8U &&
        (offset & 0x03U) == 0U;
}

Status decode(
    uint8_t capability_offset,
    uint16_t capabilities,
    uint16_t control_status,
    Info* output) {
    if (output == nullptr) return Status::InvalidArgument;
    if (!capability_layout_valid(capability_offset)) {
        return Status::CapabilityMalformed;
    }

    const uint8_t version = static_cast<uint8_t>(capabilities & 0x07U);
    if (version == 0U || version > 3U) {
        return Status::CapabilityMalformed;
    }

    *output = {};
    output->capability_offset = capability_offset;
    output->version = version;
    output->pme_clock = (capabilities & kPmeClock) != 0U;
    output->d1_supported = (capabilities & kD1Support) != 0U;
    output->d2_supported = (capabilities & kD2Support) != 0U;
    output->pme_support = static_cast<uint8_t>(
        (capabilities & kPmeSupportMask) >> kPmeSupportShift);
    output->state = static_cast<State>(control_status & kStateMask);
    output->pme_enabled = (control_status & kPmeEnable) != 0U;
    output->pme_status = (control_status & kPmeStatus) != 0U;
    return Status::Ok;
}

Status transition(
    Address address,
    uint8_t capability_offset,
    State target,
    const ConfigAccess& access,
    Info* output) {
    if (!valid_access(access)) return Status::InvalidArgument;
    if (!capability_layout_valid(capability_offset)) {
        return Status::CapabilityMalformed;
    }

    const uint8_t capabilities_offset =
        static_cast<uint8_t>(capability_offset + 2U);
    const uint8_t control_offset =
        static_cast<uint8_t>(capability_offset + 4U);
    const uint16_t capabilities =
        access.read16(address, capabilities_offset, access.context);
    const uint16_t current =
        access.read16(address, control_offset, access.context);

    Info info{};
    const Status decoded =
        decode(capability_offset, capabilities, current, &info);
    if (decoded != Status::Ok) return decoded;
    if (!state_supported(info, target)) return Status::UnsupportedState;

    // PME_Status is write-one-to-clear. Never replay a sampled '1' while
    // changing only the power-state field; preserving the sampled word
    // verbatim would acknowledge an event the power layer does not own.
    const uint16_t desired = static_cast<uint16_t>(
        (current & ~(kStateMask | kPmeStatus)) |
        static_cast<uint16_t>(target));
    access.write16(address, control_offset, desired, access.context);

    const uint16_t verified =
        access.read16(address, control_offset, access.context);
    if ((verified & kStateMask) != static_cast<uint16_t>(target)) {
        return Status::VerificationFailed;
    }
    if (output != nullptr) {
        return decode(capability_offset, capabilities, verified, output);
    }
    return Status::Ok;
}

Status query(const Device& device, Info* output) {
    if (output == nullptr) return Status::InvalidArgument;
    Capability capability{};
    if (!pci::find_capability(
            device, CapabilityId::PowerManagement, &capability)) {
        return Status::CapabilityMissing;
    }
    if (!capability_layout_valid(capability.offset)) {
        return Status::CapabilityMalformed;
    }
    return decode(
        capability.offset,
        pci::read16(device, static_cast<uint8_t>(capability.offset + 2U)),
        pci::read16(device, static_cast<uint8_t>(capability.offset + 4U)),
        output);
}

Status set_state(const Device& device, State target, Info* output) {
    Capability capability{};
    if (!pci::find_capability(
            device, CapabilityId::PowerManagement, &capability)) {
        return Status::CapabilityMissing;
    }
    return transition(
        device.address, capability.offset, target, kProductionAccess, output);
}

const char* state_name(State state) {
    switch (state) {
        case State::D0: return "D0";
        case State::D1: return "D1";
        case State::D2: return "D2";
        case State::D3Hot: return "D3hot";
    }
    return "unknown";
}

const char* status_name(Status status) {
    switch (status) {
        case Status::Ok: return "OK";
        case Status::InvalidArgument: return "INVALID_ARGUMENT";
        case Status::CapabilityMissing: return "CAPABILITY_MISSING";
        case Status::CapabilityMalformed: return "CAPABILITY_MALFORMED";
        case Status::UnsupportedState: return "UNSUPPORTED_STATE";
        case Status::VerificationFailed: return "VERIFICATION_FAILED";
    }
    return "UNKNOWN";
}

} // namespace pci::power

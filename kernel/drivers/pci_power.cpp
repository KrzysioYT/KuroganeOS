#include "pci_power.hpp"

namespace pci::power {
namespace {

constexpr uint8_t kPowerManagementCapabilityId = 0x01U;
constexpr uint16_t kPmCapabilityVersionMask = UINT16_C(0x0007);
constexpr uint16_t kPmCapabilityD1Support = UINT16_C(1) << 9U;
constexpr uint16_t kPmCapabilityD2Support = UINT16_C(1) << 10U;
constexpr uint16_t kPmCapabilityPmeSupportMask = UINT16_C(0xF800);

bool valid_access(const ConfigAccess& access) {
    return access.read16 != nullptr && access.write16 != nullptr;
}

bool valid_capability_offset(uint8_t offset) {
    // PM capability occupies six bytes: header, PMC and PMCSR.
    return offset >= 0x40U && offset <= 0xF8U &&
        (offset & 0x03U) == 0U;
}

State decode_state(uint16_t pmcsr) {
    return static_cast<State>(pmcsr & PMCSR_POWER_STATE_MASK);
}

bool state_supported(const CapabilityInfo& info, State state) {
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

uint16_t safe_pmcsr_value(uint16_t source, State state) {
    // PME Status is write-one-to-clear. Never replay a sampled one while
    // changing power state, otherwise a power-management event can be lost.
    return static_cast<uint16_t>(
        (source &
         static_cast<uint16_t>(~(PMCSR_POWER_STATE_MASK | PMCSR_PME_STATUS))) |
        static_cast<uint16_t>(state));
}

bool bus_master_active(
    Address address,
    const ConfigAccess& access) {
    return (access.read16(address, 0x04U, access.context) &
            PCI_COMMAND_BUS_MASTER) != 0U;
}

} // namespace

Status inspect_capability(
    Address address,
    uint8_t capability_offset,
    const ConfigAccess& access,
    CapabilityInfo* output) {
    if (output == nullptr || !valid_access(access)) {
        return Status::InvalidArgument;
    }
    *output = {};
    if (!valid_capability_offset(capability_offset)) {
        return Status::CapabilityMalformed;
    }

    const uint16_t header =
        access.read16(address, capability_offset, access.context);
    if (static_cast<uint8_t>(header) != kPowerManagementCapabilityId) {
        return Status::CapabilityMalformed;
    }

    const uint16_t pm_capability = access.read16(
        address,
        static_cast<uint8_t>(capability_offset + 2U),
        access.context);
    const uint8_t version =
        static_cast<uint8_t>(pm_capability & kPmCapabilityVersionMask);
    if (version == 0U || version > 3U) {
        return Status::CapabilityMalformed;
    }

    const uint16_t pmcsr = access.read16(
        address,
        static_cast<uint8_t>(capability_offset + 4U),
        access.context);

    output->offset = capability_offset;
    output->version = version;
    output->d1_supported = (pm_capability & kPmCapabilityD1Support) != 0U;
    output->d2_supported = (pm_capability & kPmCapabilityD2Support) != 0U;
    output->pme_support = static_cast<uint8_t>(
        (pm_capability & kPmCapabilityPmeSupportMask) >> 11U);
    output->current_state = decode_state(pmcsr);
    return Status::Ok;
}

Status transition(
    Address address,
    uint8_t capability_offset,
    State target,
    const ConfigAccess& access,
    Transaction* output) {
    if (output == nullptr || !valid_access(access)) {
        return Status::InvalidArgument;
    }
    *output = {};

    CapabilityInfo info{};
    const Status inspect =
        inspect_capability(address, capability_offset, access, &info);
    if (inspect != Status::Ok) return inspect;
    if (!state_supported(info, target)) return Status::UnsupportedState;

    if (target == State::D3Hot && bus_master_active(address, access)) {
        // The generic layer must never power down an actively DMA-capable
        // function. A concrete driver must quiesce DMA and clear Bus Master
        // before asking the PCI PM layer to enter D3hot.
        return Status::BusMasterActive;
    }

    const uint8_t pmcsr_offset =
        static_cast<uint8_t>(capability_offset + 4U);
    const uint16_t original =
        access.read16(address, pmcsr_offset, access.context);
    if (decode_state(original) == target) {
        output->address = address;
        output->capability_offset = capability_offset;
        output->original_pmcsr = original;
        output->target_state = target;
        output->active = true;
        return Status::Ok;
    }

    access.write16(
        address,
        pmcsr_offset,
        safe_pmcsr_value(original, target),
        access.context);
    if (decode_state(access.read16(
            address, pmcsr_offset, access.context)) != target) {
        return Status::VerificationFailed;
    }

    output->address = address;
    output->capability_offset = capability_offset;
    output->original_pmcsr = original;
    output->target_state = target;
    output->active = true;
    return Status::Ok;
}

Status restore(
    const ConfigAccess& access,
    Transaction* transaction) {
    if (transaction == nullptr || !valid_access(access)) {
        return Status::InvalidArgument;
    }
    if (!transaction->active ||
        !valid_capability_offset(transaction->capability_offset)) {
        return Status::NotActive;
    }

    const State original_state = decode_state(transaction->original_pmcsr);
    if (original_state == State::D3Hot &&
        bus_master_active(transaction->address, access)) {
        return Status::BusMasterActive;
    }

    const uint8_t pmcsr_offset =
        static_cast<uint8_t>(transaction->capability_offset + 4U);
    access.write16(
        transaction->address,
        pmcsr_offset,
        safe_pmcsr_value(transaction->original_pmcsr, original_state),
        access.context);
    if (decode_state(access.read16(
            transaction->address, pmcsr_offset, access.context)) !=
        original_state) {
        return Status::VerificationFailed;
    }

    transaction->active = false;
    return Status::Ok;
}

const char* status_name(Status status) {
    switch (status) {
        case Status::Ok: return "OK";
        case Status::InvalidArgument: return "INVALID_ARGUMENT";
        case Status::CapabilityMalformed: return "CAPABILITY_MALFORMED";
        case Status::UnsupportedState: return "UNSUPPORTED_STATE";
        case Status::BusMasterActive: return "BUS_MASTER_ACTIVE";
        case Status::VerificationFailed: return "VERIFICATION_FAILED";
        case Status::NotActive: return "NOT_ACTIVE";
    }
    return "UNKNOWN";
}

} // namespace pci::power

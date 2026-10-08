#include "pci_transport.hpp"

namespace drivers::virtio::pci_transport {
namespace {

constexpr uint16_t kStatusCapabilitiesList = UINT16_C(1) << 4U;
constexpr size_t kMaximumCapabilities = 48U;

bool header_supported(uint8_t header_type) {
    const uint8_t type = static_cast<uint8_t>(header_type & UINT8_C(0x7F));
    return type == 0x00U || type == 0x01U;
}

bool capability_offset_valid(uint8_t offset) {
    return offset >= 0x40U && offset <= 0xFCU &&
        (offset & UINT8_C(0x03)) == 0U;
}

bool access_valid(const ConfigAccess& access) {
    return access.read8 != nullptr &&
        access.read16 != nullptr &&
        access.read32 != nullptr;
}

uint8_t production_read8(pci::Address address, uint8_t offset, void*) {
    return pci::read8(address, offset);
}

uint16_t production_read16(pci::Address address, uint8_t offset, void*) {
    return pci::read16(address, offset);
}

uint32_t production_read32(pci::Address address, uint8_t offset, void*) {
    return pci::read32(address, offset);
}

const ConfigAccess kProductionAccess{
    production_read8,
    production_read16,
    production_read32,
    nullptr,
};

Capability read_vendor_capability(
    pci::Address address,
    uint8_t pointer,
    uint8_t capability_length,
    uint8_t type,
    const ConfigAccess& access,
    bool* malformed) {
    Capability result{};
    *malformed = false;
    if (capability_length < 16U || pointer > UINT8_C(0xF0)) {
        *malformed = true;
        return result;
    }

    const uint32_t second = access.read32(
        address, static_cast<uint8_t>(pointer + 4U), access.context);
    result.present = true;
    result.bar = static_cast<uint8_t>(second & UINT32_C(0xFF));
    result.offset = access.read32(
        address, static_cast<uint8_t>(pointer + 8U), access.context);
    result.length = access.read32(
        address, static_cast<uint8_t>(pointer + 12U), access.context);

    if (type == kNotifyConfigType) {
        if (capability_length < 20U || pointer > UINT8_C(0xEC)) {
            *malformed = true;
            return {};
        }
        result.notify_multiplier = access.read32(
            address, static_cast<uint8_t>(pointer + 16U), access.context);
    }

    if (result.bar > 5U || result.length == 0U ||
        result.offset > UINT32_MAX - result.length) {
        *malformed = true;
        return {};
    }
    return result;
}

} // namespace

bool region_valid(const Capability& capability, size_t maximum_length) {
    return capability.present && capability.bar <= 5U &&
        capability.length != 0U &&
        static_cast<uint64_t>(capability.length) <=
            static_cast<uint64_t>(maximum_length) &&
        capability.offset <= UINT32_MAX - capability.length;
}

Status discover(
    pci::Address address,
    uint8_t header_type,
    const ConfigAccess& access,
    Layout* output) {
    if (output == nullptr || !access_valid(access)) {
        return Status::InvalidArgument;
    }
    *output = {};
    if (!header_supported(header_type)) return Status::UnsupportedHeader;
    if ((access.read16(address, 0x06U, access.context) &
         kStatusCapabilitiesList) == 0U) {
        return Status::NoCapabilityList;
    }

    uint8_t pointer = static_cast<uint8_t>(
        access.read8(address, 0x34U, access.context) & UINT8_C(0xFC));
    if (pointer == 0U) return Status::NoCapabilityList;

    bool visited[64]{};
    for (size_t count = 0U; count < kMaximumCapabilities; ++count) {
        if (!capability_offset_valid(pointer)) return Status::MalformedList;
        const size_t visited_index = static_cast<size_t>(pointer >> 2U);
        if (visited[visited_index]) return Status::MalformedList;
        visited[visited_index] = true;

        const uint32_t header =
            access.read32(address, pointer, access.context);
        const uint8_t id =
            static_cast<uint8_t>(header & UINT32_C(0xFF));
        const uint8_t next =
            static_cast<uint8_t>((header >> 8U) & UINT32_C(0xFC));
        if (id == kVendorCapabilityId) {
            const uint8_t length =
                static_cast<uint8_t>((header >> 16U) & UINT32_C(0xFF));
            const uint8_t type =
                static_cast<uint8_t>((header >> 24U) & UINT32_C(0xFF));
            if (type >= kCommonConfigType && type <= kDeviceConfigType) {
                bool malformed = false;
                const Capability candidate = read_vendor_capability(
                    address, pointer, length, type, access, &malformed);
                if (malformed) return Status::MalformedList;
                switch (type) {
                    case kCommonConfigType:
                        if (!output->common.present) output->common = candidate;
                        break;
                    case kNotifyConfigType:
                        if (!output->notify.present) output->notify = candidate;
                        break;
                    case kIsrConfigType:
                        if (!output->isr.present) output->isr = candidate;
                        break;
                    case kDeviceConfigType:
                        if (!output->device.present) output->device = candidate;
                        break;
                    default:
                        break;
                }
            }
        }

        if (next == 0U) {
            return output->common.present && output->notify.present
                ? Status::Ok
                : Status::MissingRequiredCapability;
        }
        pointer = next;
    }
    return Status::MalformedList;
}

Status discover(const pci::Device& device, Layout* output) {
    return discover(
        device.address, device.header_type, kProductionAccess, output);
}

const char* status_name(Status status) {
    switch (status) {
        case Status::Ok: return "OK";
        case Status::InvalidArgument: return "INVALID_ARGUMENT";
        case Status::UnsupportedHeader: return "UNSUPPORTED_HEADER";
        case Status::NoCapabilityList: return "NO_CAPABILITY_LIST";
        case Status::MalformedList: return "MALFORMED_LIST";
        case Status::MissingRequiredCapability:
            return "MISSING_REQUIRED_CAPABILITY";
    }
    return "UNKNOWN";
}

} // namespace drivers::virtio::pci_transport

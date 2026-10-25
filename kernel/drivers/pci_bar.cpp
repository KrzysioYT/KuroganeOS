#include "pci_bar.hpp"

namespace pci::bar {
namespace {

constexpr uint32_t kIoIndicator = UINT32_C(1);
constexpr uint32_t kMemoryTypeMask = UINT32_C(3) << 1U;
constexpr uint32_t kMemoryType32 = UINT32_C(0);
constexpr uint32_t kMemoryType64 = UINT32_C(2) << 1U;
constexpr uint32_t kPrefetchable = UINT32_C(1) << 3U;
constexpr uint32_t kIoAddressMask = UINT32_C(0xFFFFFFFC);
constexpr uint32_t kMemoryAddressMask = UINT32_C(0xFFFFFFF0);

bool valid_access(const ConfigAccess& access) {
    return access.read16 != nullptr && access.read32 != nullptr &&
        access.write32 != nullptr;
}

uint8_t bar_count(uint8_t header_type) {
    switch (header_type & 0x7FU) {
        case 0x00U: return 6U;
        case 0x01U: return 2U;
        default: return 0U;
    }
}

uint8_t bar_offset(uint8_t index) {
    return static_cast<uint8_t>(0x10U + index * 4U);
}

bool is_memory64(uint32_t value) {
    return (value & kIoIndicator) == 0U &&
        (value & kMemoryTypeMask) == kMemoryType64;
}

bool is_power_of_two(uint64_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

uint16_t real_read16(Address address, uint8_t offset, void*) {
    return pci::read16(address, offset);
}

uint32_t real_read32(Address address, uint8_t offset, void*) {
    return pci::read32(address, offset);
}

void real_write32(Address address, uint8_t offset, uint32_t value, void*) {
    pci::write32(address, offset, value);
}

ConfigAccess real_access() {
    return {real_read16, real_read32, real_write32, nullptr};
}

} // namespace

Status decode(
    uint8_t index,
    uint32_t original_low,
    uint32_t original_high,
    uint32_t probe_low,
    uint32_t probe_high,
    Info* output) {
    if (output == nullptr || index >= 6U) return Status::InvalidArgument;
    *output = {};

    Info info{};
    info.index = index;
    info.consumed_bars = 1U;

    uint64_t address_mask = 0U;
    if ((original_low & kIoIndicator) != 0U) {
        info.kind = Kind::Io;
        uint32_t io_mask = probe_low & kIoAddressMask;
        if ((io_mask & UINT32_C(0xFFFF0000)) == 0U) {
            io_mask |= UINT32_C(0xFFFF0000);
        }
        address_mask = io_mask;
        info.physical_address = original_low & kIoAddressMask;
    } else {
        const uint32_t memory_type = original_low & kMemoryTypeMask;
        info.prefetchable = (original_low & kPrefetchable) != 0U;
        if (memory_type == kMemoryType32) {
            info.kind = Kind::Memory32;
            address_mask = probe_low & kMemoryAddressMask;
            info.physical_address = original_low & kMemoryAddressMask;
        } else if (memory_type == kMemoryType64) {
            if (index >= 5U) return Status::UnsupportedType;
            info.kind = Kind::Memory64;
            info.consumed_bars = 2U;
            address_mask = (static_cast<uint64_t>(probe_high) << 32U) |
                static_cast<uint64_t>(probe_low & kMemoryAddressMask);
            info.physical_address =
                (static_cast<uint64_t>(original_high) << 32U) |
                static_cast<uint64_t>(original_low & kMemoryAddressMask);
        } else {
            return Status::UnsupportedType;
        }
    }

    if (address_mask == 0U) return Status::Unimplemented;
    const uint64_t size = info.kind == Kind::Memory64
        ? (~address_mask) + UINT64_C(1)
        : static_cast<uint64_t>(
            (~static_cast<uint32_t>(address_mask)) + UINT32_C(1));
    if (!is_power_of_two(size) ||
        (info.kind == Kind::Io && size < 4U) ||
        (info.kind != Kind::Io && size < 16U)) {
        return Status::InvalidSize;
    }
    if (info.physical_address == 0U) return Status::Unassigned;
    if ((info.physical_address & (size - 1U)) != 0U) {
        return Status::MisalignedBase;
    }
    info.size = size;
    *output = info;
    return Status::Ok;
}

Status probe_disabled(
    Address address,
    uint8_t header_type,
    uint8_t index,
    const ConfigAccess& access,
    Info* output) {
    if (output == nullptr || !valid_access(access)) {
        return Status::InvalidArgument;
    }
    *output = {};
    const uint8_t count = bar_count(header_type);
    if (count == 0U) return Status::UnsupportedHeader;
    if (index >= count) return Status::InvalidArgument;
    if ((access.read16(address, 0x04U, access.context) &
            COMMAND_DECODE_MASK) != 0U) {
        return Status::DecodingEnabled;
    }

    for (uint8_t current = 0U; current < index;) {
        const uint32_t prior = access.read32(
            address, bar_offset(current), access.context);
        if (is_memory64(prior)) {
            if (current + 1U == index) return Status::UpperHalf;
            current = static_cast<uint8_t>(current + 2U);
        } else {
            ++current;
        }
    }

    const uint8_t low_offset = bar_offset(index);
    const uint32_t original_low =
        access.read32(address, low_offset, access.context);
    const bool wide = is_memory64(original_low);
    if (wide && index + 1U >= count) return Status::UnsupportedType;

    uint32_t original_high = 0U;
    uint32_t probe_high = 0U;
    if (wide) {
        original_high = access.read32(
            address, static_cast<uint8_t>(low_offset + 4U), access.context);
        access.write32(
            address,
            static_cast<uint8_t>(low_offset + 4U),
            UINT32_MAX,
            access.context);
    }
    access.write32(address, low_offset, UINT32_MAX, access.context);
    const uint32_t probe_low =
        access.read32(address, low_offset, access.context);
    if (wide) {
        probe_high = access.read32(
            address, static_cast<uint8_t>(low_offset + 4U), access.context);
        access.write32(
            address,
            static_cast<uint8_t>(low_offset + 4U),
            original_high,
            access.context);
    }
    access.write32(address, low_offset, original_low, access.context);

    return decode(
        index,
        original_low,
        original_high,
        probe_low,
        probe_high,
        output);
}

Status probe_disabled(const Device& device, uint8_t index, Info* output) {
    return probe_disabled(
        device.address, device.header_type, index, real_access(), output);
}

const char* status_name(Status status) {
    switch (status) {
        case Status::Ok: return "OK";
        case Status::InvalidArgument: return "INVALID_ARGUMENT";
        case Status::UnsupportedHeader: return "UNSUPPORTED_HEADER";
        case Status::DecodingEnabled: return "DECODING_ENABLED";
        case Status::UpperHalf: return "UPPER_HALF";
        case Status::UnsupportedType: return "UNSUPPORTED_TYPE";
        case Status::Unimplemented: return "UNIMPLEMENTED";
        case Status::Unassigned: return "UNASSIGNED";
        case Status::InvalidSize: return "INVALID_SIZE";
        case Status::MisalignedBase: return "MISALIGNED_BASE";
    }
    return "UNKNOWN";
}

} // namespace pci::bar

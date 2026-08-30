#include "pci.hpp"

#include "../arch/x86_64/io.hpp"

namespace pci {

namespace {
constexpr size_t kMaximumDevices = 128;
Device g_devices[kMaximumDevices]{};
size_t g_device_count = 0;

uint32_t configuration_address(Address address, uint8_t offset) {
    return 0x80000000u |
           (static_cast<uint32_t>(address.bus) << 16) |
           (static_cast<uint32_t>(address.slot) << 11) |
           (static_cast<uint32_t>(address.function) << 8) |
           (offset & 0xFCu);
}

bool function_exists(Address address) {
    return read16(address, 0x00) != 0xFFFFu;
}


bool capability_header_supported(const Device& device) {
    const uint8_t type = static_cast<uint8_t>(device.header_type & 0x7FU);
    return type == 0x00U || type == 0x01U;
}

bool capability_list_present(const Device& device) {
    constexpr uint16_t kStatusCapabilitiesList = UINT16_C(1) << 4U;
    return (read16(device.address, 0x06U) & kStatusCapabilitiesList) != 0U;
}

bool valid_capability_offset(uint8_t offset) {
    return offset >= 0x40U && offset <= 0xFCU && (offset & 0x03U) == 0U;
}

void record_function(Address address) {
    if (g_device_count >= kMaximumDevices || !function_exists(address)) {
        return;
    }
    Device& device = g_devices[g_device_count++];
    device.address = address;
    device.vendor_id = read16(address, 0x00);
    device.device_id = read16(address, 0x02);
    device.revision = read8(address, 0x08);
    device.programming_interface = read8(address, 0x09);
    device.subclass = read8(address, 0x0A);
    device.class_code = read8(address, 0x0B);
    device.header_type = read8(address, 0x0E);
    device.interrupt_line = read8(address, 0x3C);
}
} // namespace

uint32_t read32(Address address, uint8_t offset) {
    arch::out32(0xCF8, configuration_address(address, offset));
    return arch::in32(0xCFC);
}

uint16_t read16(Address address, uint8_t offset) {
    const uint32_t value = read32(address, offset);
    return static_cast<uint16_t>(
        (value >> ((offset & 2u) * 8u)) & 0xFFFFu);
}

uint8_t read8(Address address, uint8_t offset) {
    const uint32_t value = read32(address, offset);
    return static_cast<uint8_t>(
        (value >> ((offset & 3u) * 8u)) & 0xFFu);
}

void write32(Address address, uint8_t offset, uint32_t value) {
    arch::out32(0xCF8, configuration_address(address, offset));
    arch::out32(0xCFC, value);
}

void write16(Address address, uint8_t offset, uint16_t value) {
    const uint8_t aligned_offset = offset & 0xFCu;
    const uint32_t shift = (offset & 2u) * 8u;
    uint32_t current = read32(address, aligned_offset);
    current &= ~(0xFFFFu << shift);
    current |= static_cast<uint32_t>(value) << shift;
    write32(address, aligned_offset, current);
}

void scan() {
    g_device_count = 0;
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            Address first{static_cast<uint8_t>(bus), slot, 0};
            if (!function_exists(first)) {
                continue;
            }
            record_function(first);
            const uint8_t functions =
                (read8(first, 0x0E) & 0x80u) != 0 ? 8 : 1;
            for (uint8_t function = 1; function < functions; ++function) {
                record_function(
                    Address{static_cast<uint8_t>(bus), slot, function});
            }
        }
    }
}

size_t device_count() {
    return g_device_count;
}

const Device* device_at(size_t index) {
    return index < g_device_count ? &g_devices[index] : nullptr;
}

const Device* find(uint16_t vendor_id, uint16_t device_id,
                   size_t occurrence) {
    for (size_t i = 0; i < g_device_count; ++i) {
        if (g_devices[i].vendor_id == vendor_id &&
            g_devices[i].device_id == device_id) {
            if (occurrence == 0) {
                return &g_devices[i];
            }
            --occurrence;
        }
    }
    return nullptr;
}

const Device* find_class(uint8_t class_code, uint8_t subclass,
                         size_t occurrence) {
    for (size_t i = 0; i < g_device_count; ++i) {
        if (g_devices[i].class_code == class_code &&
            g_devices[i].subclass == subclass) {
            if (occurrence == 0) {
                return &g_devices[i];
            }
            --occurrence;
        }
    }
    return nullptr;
}

void visit(VisitCallback callback, void* context) {
    if (!callback) {
        return;
    }
    for (size_t i = 0; i < g_device_count; ++i) {
        if (!callback(g_devices[i], context)) {
            return;
        }
    }
}

CapabilityWalkStatus visit_capabilities(
    const Device& device,
    CapabilityCallback callback,
    void* context) {
    if (callback == nullptr) return CapabilityWalkStatus::InvalidArgument;
    if (!capability_header_supported(device)) {
        return CapabilityWalkStatus::UnsupportedHeader;
    }
    if (!capability_list_present(device)) {
        return CapabilityWalkStatus::NotPresent;
    }

    bool visited[64]{};
    uint8_t offset = static_cast<uint8_t>(read8(device.address, 0x34U) & 0xFCU);
    if (offset == 0U) return CapabilityWalkStatus::NotPresent;
    for (size_t count = 0U; count < MAX_CAPABILITIES_PER_DEVICE; ++count) {
        if (!valid_capability_offset(offset)) {
            return CapabilityWalkStatus::MalformedList;
        }
        const size_t visited_index = static_cast<size_t>(offset >> 2U);
        if (visited[visited_index]) return CapabilityWalkStatus::MalformedList;
        visited[visited_index] = true;

        Capability capability{};
        capability.id = read8(device.address, offset);
        capability.offset = offset;
        capability.next = static_cast<uint8_t>(
            read8(device.address, static_cast<uint8_t>(offset + 1U)) & 0xFCU);
        if (!callback(device, capability, context)) {
            return CapabilityWalkStatus::IterationStopped;
        }
        if (capability.next == 0U) return CapabilityWalkStatus::Ok;
        offset = capability.next;
    }
    return CapabilityWalkStatus::MalformedList;
}

bool find_capability(
    const Device& device,
    CapabilityId id,
    Capability* output) {
    if (output == nullptr || !capability_header_supported(device) ||
        !capability_list_present(device)) {
        return false;
    }
    *output = {};
    bool visited[64]{};
    uint8_t offset = static_cast<uint8_t>(read8(device.address, 0x34U) & 0xFCU);
    for (size_t count = 0U;
         count < MAX_CAPABILITIES_PER_DEVICE && offset != 0U;
         ++count) {
        if (!valid_capability_offset(offset)) return false;
        const size_t visited_index = static_cast<size_t>(offset >> 2U);
        if (visited[visited_index]) return false;
        visited[visited_index] = true;
        Capability capability{};
        capability.id = read8(device.address, offset);
        capability.offset = offset;
        capability.next = static_cast<uint8_t>(
            read8(device.address, static_cast<uint8_t>(offset + 1U)) & 0xFCU);
        if (capability.id == static_cast<uint8_t>(id)) {
            *output = capability;
            return true;
        }
        offset = capability.next;
    }
    return false;
}

bool read_msi_info(const Device& device, MsiInfo* output) {
    if (output == nullptr) return false;
    *output = {};
    Capability capability{};
    if (!find_capability(device, CapabilityId::Msi, &capability)) return false;
    const uint16_t control = read16(
        device.address, static_cast<uint8_t>(capability.offset + 2U));
    output->offset = capability.offset;
    output->enabled = (control & UINT16_C(1)) != 0U;
    output->multiple_message_capable = static_cast<uint8_t>((control >> 1U) & 0x07U);
    output->multiple_message_enabled = static_cast<uint8_t>((control >> 4U) & 0x07U);
    output->address_64_bit = (control & (UINT16_C(1) << 7U)) != 0U;
    output->per_vector_masking = (control & (UINT16_C(1) << 8U)) != 0U;
    return true;
}

bool read_msix_info(const Device& device, MsiXInfo* output) {
    if (output == nullptr) return false;
    *output = {};
    Capability capability{};
    if (!find_capability(device, CapabilityId::MsiX, &capability)) return false;
    const uint16_t control = read16(
        device.address, static_cast<uint8_t>(capability.offset + 2U));
    const uint32_t table = read32(
        device.address, static_cast<uint8_t>(capability.offset + 4U));
    const uint32_t pba = read32(
        device.address, static_cast<uint8_t>(capability.offset + 8U));
    output->offset = capability.offset;
    output->table_size = static_cast<uint16_t>((control & UINT16_C(0x07ff)) + 1U);
    output->function_mask = (control & (UINT16_C(1) << 14U)) != 0U;
    output->enabled = (control & (UINT16_C(1) << 15U)) != 0U;
    output->table_bar = static_cast<uint8_t>(table & 0x07U);
    output->table_offset = table & ~UINT32_C(0x07);
    output->pending_bit_array_bar = static_cast<uint8_t>(pba & 0x07U);
    output->pending_bit_array_offset = pba & ~UINT32_C(0x07);
    return true;
}

const char* capability_walk_status_name(CapabilityWalkStatus status) {
    switch (status) {
        case CapabilityWalkStatus::Ok: return "OK";
        case CapabilityWalkStatus::NotPresent: return "NOT_PRESENT";
        case CapabilityWalkStatus::UnsupportedHeader: return "UNSUPPORTED_HEADER";
        case CapabilityWalkStatus::InvalidArgument: return "INVALID_ARGUMENT";
        case CapabilityWalkStatus::MalformedList: return "MALFORMED_LIST";
        case CapabilityWalkStatus::IterationStopped: return "ITERATION_STOPPED";
    }
    return "UNKNOWN";
}

uint64_t bar_address(const Device& device, uint8_t bar_index, bool* is_io) {
    if (is_io) {
        *is_io = false;
    }

    uint8_t bar_count = 0;
    switch (device.header_type & 0x7Fu) {
    case 0x00:
        bar_count = 6;
        break;
    case 0x01:
        bar_count = 2;
        break;
    default:
        break;
    }
    if (bar_index >= bar_count) {
        return 0;
    }

    /*
     * Walk from BAR0 so an index naming the upper dword of a 64-bit BAR is
     * rejected even when those high address bits happen to resemble a BAR.
     */
    for (uint8_t index = 0; index < bar_count;) {
        const uint8_t offset =
            static_cast<uint8_t>(0x10u + index * 4u);
        const uint32_t low = read32(device.address, offset);
        const bool io_space = (low & 1u) != 0;
        const uint32_t memory_type = (low >> 1) & 0x3u;

        if (index == bar_index) {
            if (low == UINT32_MAX) {
                return 0;
            }
            if (io_space) {
                if (is_io) {
                    *is_io = true;
                }
                return low & ~UINT32_C(0x3);
            }
            if (memory_type == 0x3u) {
                return 0;
            }

            uint64_t address = low & ~UINT32_C(0xF);
            if (memory_type == 0x2u) {
                if (index + 1u >= bar_count) {
                    return 0;
                }
                const uint32_t high = read32(
                    device.address, static_cast<uint8_t>(offset + 4u));
                address |= static_cast<uint64_t>(high) << 32;
            }
            return address;
        }

        if (!io_space && low != UINT32_MAX &&
            memory_type == 0x2u) {
            if (index + 1u >= bar_count) {
                return 0;
            }
            index = static_cast<uint8_t>(index + 2u);
        } else {
            ++index;
        }
    }
    return 0;
}

void enable_bus_mastering(const Device& device) {
    uint16_t command = read16(device.address, 0x04);
    command |= 0x0006u;
    write16(device.address, 0x04, command);
}

} // namespace pci

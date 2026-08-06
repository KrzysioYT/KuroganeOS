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

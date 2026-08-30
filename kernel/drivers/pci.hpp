#pragma once

#include <stddef.h>
#include <stdint.h>

namespace pci {

struct Address {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
};

struct Device {
    Address address;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t programming_interface;
    uint8_t revision;
    uint8_t header_type;
    uint8_t interrupt_line;
};

constexpr size_t MAX_CAPABILITIES_PER_DEVICE = 48U;

enum class CapabilityId : uint8_t {
    PowerManagement = 0x01U,
    Msi = 0x05U,
    PciExpress = 0x10U,
    MsiX = 0x11U,
};

enum class CapabilityWalkStatus : uint8_t {
    Ok = 0,
    NotPresent,
    UnsupportedHeader,
    InvalidArgument,
    MalformedList,
    IterationStopped,
};

struct Capability {
    uint8_t id;
    uint8_t offset;
    uint8_t next;
};

struct MsiInfo {
    uint8_t offset;
    bool enabled;
    bool address_64_bit;
    bool per_vector_masking;
    uint8_t multiple_message_capable;
    uint8_t multiple_message_enabled;
};

struct MsiXInfo {
    uint8_t offset;
    bool enabled;
    bool function_mask;
    uint16_t table_size;
    uint8_t table_bar;
    uint32_t table_offset;
    uint8_t pending_bit_array_bar;
    uint32_t pending_bit_array_offset;
};

using VisitCallback = bool (*)(const Device& device, void* context);
using CapabilityCallback = bool (*)(
    const Device& device,
    const Capability& capability,
    void* context);

uint32_t read32(Address address, uint8_t offset);
uint16_t read16(Address address, uint8_t offset);
uint8_t read8(Address address, uint8_t offset);
void write32(Address address, uint8_t offset, uint32_t value);
void write16(Address address, uint8_t offset, uint16_t value);

/*
 * Convenience overloads keep drivers on the validated Device descriptor while
 * preserving the same config-space implementation and ABI underneath.
 */
static inline uint32_t read32(const Device& device, uint8_t offset) {
    return read32(device.address, offset);
}

static inline uint16_t read16(const Device& device, uint8_t offset) {
    return read16(device.address, offset);
}

static inline uint8_t read8(const Device& device, uint8_t offset) {
    return read8(device.address, offset);
}

static inline void write32(
    const Device& device,
    uint8_t offset,
    uint32_t value) {
    write32(device.address, offset, value);
}

static inline void write16(
    const Device& device,
    uint8_t offset,
    uint16_t value) {
    write16(device.address, offset, value);
}

void scan();
size_t device_count();
const Device* device_at(size_t index);
const Device* find(uint16_t vendor_id, uint16_t device_id,
                   size_t occurrence = 0);
const Device* find_class(uint8_t class_code, uint8_t subclass,
                         size_t occurrence = 0);
void visit(VisitCallback callback, void* context);
CapabilityWalkStatus visit_capabilities(
    const Device& device,
    CapabilityCallback callback,
    void* context);
bool find_capability(
    const Device& device,
    CapabilityId id,
    Capability* output);
bool read_msi_info(const Device& device, MsiInfo* output);
bool read_msix_info(const Device& device, MsiXInfo* output);
const char* capability_walk_status_name(CapabilityWalkStatus status);
uint64_t bar_address(const Device& device, uint8_t bar_index,
                     bool* is_io = nullptr);
void enable_bus_mastering(const Device& device);

} // namespace pci

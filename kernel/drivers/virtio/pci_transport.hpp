#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../pci.hpp"

namespace drivers::virtio::pci_transport {

constexpr uint16_t kVirtioVendor = UINT16_C(0x1AF4);
constexpr uint8_t kVendorCapabilityId = UINT8_C(0x09);
constexpr uint8_t kCommonConfigType = 1U;
constexpr uint8_t kNotifyConfigType = 2U;
constexpr uint8_t kIsrConfigType = 3U;
constexpr uint8_t kDeviceConfigType = 4U;

enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    UnsupportedHeader,
    NoCapabilityList,
    MalformedList,
    MissingRequiredCapability,
};

struct Capability {
    bool present;
    uint8_t bar;
    uint32_t offset;
    uint32_t length;
    uint32_t notify_multiplier;
};

struct Layout {
    Capability common;
    Capability notify;
    Capability isr;
    Capability device;
};

struct ConfigAccess {
    uint8_t (*read8)(pci::Address address, uint8_t offset, void* context);
    uint16_t (*read16)(pci::Address address, uint8_t offset, void* context);
    uint32_t (*read32)(pci::Address address, uint8_t offset, void* context);
    void* context;
};

bool region_valid(const Capability& capability, size_t maximum_length);
Status discover(
    pci::Address address,
    uint8_t header_type,
    const ConfigAccess& access,
    Layout* output);
Status discover(const pci::Device& device, Layout* output);
const char* status_name(Status status);

} // namespace drivers::virtio::pci_transport

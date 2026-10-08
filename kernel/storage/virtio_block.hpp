#pragma once

#include <stdint.h>

#include "block_device.hpp"

namespace storage::virtio_block {

enum class Status : uint8_t {
    Ok = 0,
    AlreadyInitialized,
    NoDevice,
    UnsupportedTransport,
    PciCommandFailed,
    MappingFailed,
    DeviceResetFailed,
    FeatureNegotiationFailed,
    MissingDeviceConfig,
    UnsupportedGeometry,
    QueueUnavailable,
    DmaAllocationFailed,
    QueueConfigurationFailed,
    DeviceFault,
    DeviceBusy,
    TimedOut,
    IoError,
    UnsupportedOperation,
    RegistryFailed,
    ResourceReleaseFailed,
};

struct DeviceInfo {
    uint8_t pci_bus;
    uint8_t pci_slot;
    uint8_t pci_function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t block_size;
    uint64_t block_count;
    uint64_t capacity_512_sectors;
    bool read_only;
    bool flush_supported;
};

Status initialize();
void shutdown();

bool initialized();
const DeviceInfo* device_info();
const block::Device* block_device();
const char* status_message(Status status);

} // namespace storage::virtio_block

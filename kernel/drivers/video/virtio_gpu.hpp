#pragma once

#include <stdint.h>

namespace drivers::video::virtio_gpu {

enum class Status : uint8_t {
    Ok = 0,
    NotInitialized,
    AlreadyInitialized,
    NoDevice,
    UnsupportedTransport,
    MappingFailed,
    FeatureNegotiationFailed,
    QueueUnavailable,
    QueueAllocationFailed,
    QueueConfigurationFailed,
    CommandTimeout,
    InvalidResponse,
    DeviceFault,
};

struct DisplayInfo {
    bool detected;
    bool initialized;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t width;
    uint32_t height;
    uint32_t scanout_id;
    uint32_t enabled_scanouts;
};

// Brings up the modern VirtIO PCI transport and control queue, then proves
// native command submission by issuing VIRTIO_GPU_CMD_GET_DISPLAY_INFO.
// Scanout ownership remains with UEFI GOP until the 2D resource path is ready.
Status initialize();
bool detected();
bool initialized();
Status last_status();
const DisplayInfo& display_info();
const char* status_message(Status status);

} // namespace drivers::video::virtio_gpu

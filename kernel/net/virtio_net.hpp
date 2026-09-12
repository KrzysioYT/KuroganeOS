#pragma once

#include "network.hpp"

#include <stddef.h>
#include <stdint.h>

namespace net::virtio_net {

enum class Status : uint8_t {
    Ok = 0,
    NotInitialized,
    AlreadyInitialized,
    NoDevice,
    UnsupportedTransport,
    MissingCapability,
    MappingFailed,
    FeatureNegotiationFailed,
    QueueUnavailable,
    QueueAllocationFailed,
    QueueConfigurationFailed,
    InvalidArgument,
    FrameTooLarge,
    WouldBlock,
    DeviceFault,
    PciCommandFailed,
    DeviceResetFailed,
    DeviceCleanupFailed,
    QueueInterruptFailed
};

// Kernel diagnostics only. Polling remains available when interrupt resources
// cannot be obtained. A queue rejecting an assigned vector fails initialization.
enum class InterruptStatus : uint8_t {
    NotInitialized = 0,
    MsiXEnabled,
    LocalApicUnavailable,
    CapabilityUnavailable,
    CapabilityMalformed,
    BarUnavailable,
    MappingUnavailable,
    RouteUnavailable,
    QueueRejected,
    CleanupFailed
};

struct InterruptDiagnostics {
    InterruptStatus status;
    uint8_t vector;
    uint64_t delivered;
    bool pending;
};

InterruptDiagnostics interrupt_diagnostics();
const char* interrupt_status_name(InterruptStatus status);

Status initialize();
bool initialized();
bool detected();
Status last_status();
NetworkInterface* interface();
const char* status_message(Status status);

} // namespace net::virtio_net

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
    DeviceFault
};

Status initialize();
bool initialized();
bool detected();
Status last_status();
NetworkInterface* interface();
const char* status_message(Status status);

} // namespace net::virtio_net

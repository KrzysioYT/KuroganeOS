#pragma once

#include "network.hpp"

#include <stdint.h>

namespace net::pcnet {

enum class Status : uint8_t {
    Ok = 0,
    AlreadyInitialized,
    NotInitialized,
    NotFound,
    InvalidBar,
    ResetTimedOut,
    InvalidMac,
    DmaAllocationFailed,
    InitializationTimedOut,
    LinkUnavailable,
    DeviceError,
};

Status initialize();
bool ready();
NetworkInterface* interface();
const MacAddress* hardware_address();
uint64_t transmitted_frames();
uint64_t received_frames();
uint64_t dropped_frames();
const char* status_message(Status status);

} // namespace net::pcnet

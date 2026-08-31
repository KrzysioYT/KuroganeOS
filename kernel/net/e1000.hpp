#pragma once

#include "network.hpp"

#include <stddef.h>
#include <stdint.h>

namespace net::e1000 {

enum class Status : uint8_t {
    Ok = 0,
    AlreadyInitialized,
    NotInitialized,
    NotFound,
    UnsupportedDevice,
    InvalidBar,
    MmioMapFailed,
    ResetTimedOut,
    InvalidMac,
    DmaAllocationFailed,
    LinkDown,
    TransmitTimedOut,
    DeviceError,
};

Status initialize();
bool ready();
bool link_up();
NetworkInterface* interface();
const MacAddress* hardware_address();
uint64_t transmitted_frames();
uint64_t received_frames();
uint64_t dropped_frames();
const char* status_message(Status status);

} // namespace net::e1000

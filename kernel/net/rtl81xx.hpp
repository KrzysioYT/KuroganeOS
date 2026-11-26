#pragma once

#include "network.hpp"
#include <stdint.h>

namespace net::rtl81xx {

enum class Status : uint8_t {
    Ok = 0,
    AlreadyInitialized,
    NotFound,
    InvalidBar,
    MappingFailed,
    ResetTimedOut,
    InvalidMac,
    DmaAllocationFailed,
    LinkDown,
    DeviceFault,
    ResourceReleaseFailed,
    Busy,
    UnsupportedRevision,
    DmaQuiescenceUnverified
};

Status initialize();
Status shutdown();
bool ready();
bool link_up();
NetworkInterface* interface();
Status last_status();
uint16_t mac_xid();
const char* status_message(Status status);

} // namespace net::rtl81xx

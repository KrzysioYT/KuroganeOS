#pragma once

#include "network.hpp"

#include <stdint.h>

namespace net::physical {

enum class Driver : uint8_t {
    None = 0,
    VirtioNet,
    E1000,
    Pcnet,
};

enum class Status : uint8_t {
    Ok = 0,
    AlreadyInitialized,
    NotFound,
    DeviceUnavailable,
};

Status initialize();
bool ready();
bool link_up();
bool detected();
Driver driver();
NetworkInterface* interface();
const char* name();
const char* status_message(Status status);

} // namespace net::physical

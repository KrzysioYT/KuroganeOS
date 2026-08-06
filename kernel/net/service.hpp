#pragma once

#include "network.hpp"

#include <stddef.h>
#include <stdint.h>

namespace net::service {

Status initialize();
bool ready();
Status poll(size_t budget = 8, size_t* processed = nullptr);
Status ping_loopback(uint16_t sequence, PingReply* reply = nullptr);
Status stats(NetworkStats* output);
const IPv4Config* configuration();

} // namespace net::service

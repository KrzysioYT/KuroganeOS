#pragma once

#include "network.hpp"

namespace net::dhcp {

struct Lease {
    IPv4Config configuration;
    IPv4Address dns_server;
    IPv4Address server_identifier;
    uint32_t lease_seconds;
};

Status acquire(NetworkInterface* interface, Lease* out_lease);

} // namespace net::dhcp

#pragma once

#include "network.hpp"

#include <stddef.h>
#include <stdint.h>

namespace net::service {

Status initialize();
bool ready();
Status poll(size_t budget = 8, size_t* processed = nullptr);
Status ping_loopback(uint16_t sequence, PingReply* reply = nullptr);
Status ping_gateway(uint16_t sequence, PingReply* reply = nullptr);
Status ping_address(
    const IPv4Address& address,
    uint16_t sequence,
    PingReply* reply = nullptr);
Status resolve_a(const char* name, IPv4Address* out_address);
Status tcp_connect_probe(
    const IPv4Address& address,
    uint16_t port,
    const char* host_name);

// Transitional 3.3.3 browser transport. It performs one bounded HTTP/1.0 GET
// over the native E1000/IPv4/TCP stack. HTTPS and a general socket ABI are
// intentionally not pretended here; those require TLS and asynchronous socket
// ownership before a Chromium-class engine can be ported safely.
Status http_get(
    const char* host_name,
    const char* path,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length,
    uint16_t* out_http_status);

Status stats(NetworkStats* output);
const IPv4Config* configuration();
const IPv4Address* dns_server();
uint32_t lease_seconds();
Status list_neighbors(NeighborCallback callback, void* context);
bool physical_interface();
bool physical_device_detected();
Status physical_status();
bool dhcp_configured();
const char* interface_name();

} // namespace net::service

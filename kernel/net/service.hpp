#pragma once

#include "network.hpp"

#include <stddef.h>
#include <stdint.h>

namespace net::service {

Status initialize();
bool ready();
Status poll(size_t budget = 8, size_t* processed = nullptr);
Status socket_send_udp(
    const IPv4Address& destination,
    uint16_t source_port,
    uint16_t destination_port,
    const uint8_t* payload,
    size_t payload_length);
Status socket_take_udp(UdpDatagram* datagram);
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

// Bounded HTTP/1.0 transports used by Kurogane Web. HTTPS uses the pinned
// Mbed TLS client with required CA/hostname verification and Kurogane RTC
// certificate-validity checks; it never silently downgrades to plaintext.
Status http_get(
    const char* host_name,
    const char* path,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length,
    uint16_t* out_http_status);
Status https_get(
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

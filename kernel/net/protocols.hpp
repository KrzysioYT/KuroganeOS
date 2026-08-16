#pragma once

#include "network.hpp"

#include <stddef.h>
#include <stdint.h>

namespace net {

constexpr uint8_t IPV4_PROTOCOL_TCP = 6U;
constexpr uint8_t IPV4_PROTOCOL_UDP = 17U;
constexpr size_t UDP_HEADER_SIZE = 8U;
constexpr size_t TCP_MIN_HEADER_SIZE = 20U;
constexpr size_t UDP_MAX_PAYLOAD =
    ETHERNET_MTU - IPV4_MIN_HEADER_SIZE - UDP_HEADER_SIZE;
constexpr size_t TCP_MAX_PAYLOAD =
    ETHERNET_MTU - IPV4_MIN_HEADER_SIZE - TCP_MIN_HEADER_SIZE;

struct UdpDatagramView {
    uint16_t source_port;
    uint16_t destination_port;
    const uint8_t* payload;
    size_t payload_length;
};

Status parse_udp(
    const IPv4Address& source,
    const IPv4Address& destination,
    const uint8_t* packet,
    size_t packet_length,
    UdpDatagramView* out_datagram);
Status serialize_udp(
    const IPv4Address& source,
    const IPv4Address& destination,
    uint16_t source_port,
    uint16_t destination_port,
    const uint8_t* payload,
    size_t payload_length,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length);

enum TcpFlag : uint8_t {
    TcpFin = UINT8_C(1) << 0U,
    TcpSyn = UINT8_C(1) << 1U,
    TcpRst = UINT8_C(1) << 2U,
    TcpPsh = UINT8_C(1) << 3U,
    TcpAck = UINT8_C(1) << 4U,
};

struct TcpSegmentView {
    uint16_t source_port;
    uint16_t destination_port;
    uint32_t sequence;
    uint32_t acknowledgement;
    uint8_t flags;
    uint16_t window;
    const uint8_t* payload;
    size_t payload_length;
};

Status parse_tcp(
    const IPv4Address& source,
    const IPv4Address& destination,
    const uint8_t* packet,
    size_t packet_length,
    TcpSegmentView* out_segment);
Status serialize_tcp(
    const IPv4Address& source,
    const IPv4Address& destination,
    uint16_t source_port,
    uint16_t destination_port,
    uint32_t sequence,
    uint32_t acknowledgement,
    uint8_t flags,
    uint16_t window,
    const uint8_t* payload,
    size_t payload_length,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length);

enum class DhcpMessageType : uint8_t {
    Discover = 1U,
    Offer = 2U,
    Request = 3U,
    Decline = 4U,
    Ack = 5U,
    Nak = 6U,
};

struct DhcpMessageView {
    DhcpMessageType type;
    uint32_t transaction_id;
    IPv4Address offered_address;
    IPv4Address server_identifier;
    IPv4Address netmask;
    IPv4Address gateway;
    IPv4Address dns_server;
    uint32_t lease_seconds;
    bool has_server_identifier;
    bool has_netmask;
    bool has_gateway;
    bool has_dns_server;
};

Status serialize_dhcp_discover(
    uint32_t transaction_id,
    const MacAddress& client,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length);
Status serialize_dhcp_request(
    uint32_t transaction_id,
    const MacAddress& client,
    const IPv4Address& requested_address,
    const IPv4Address& server_identifier,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length);
Status parse_dhcp_reply(
    const uint8_t* packet,
    size_t packet_length,
    uint32_t expected_transaction_id,
    const MacAddress& expected_client,
    DhcpMessageView* out_message);

struct DnsAnswer {
    uint16_t transaction_id;
    IPv4Address address;
    uint32_t ttl_seconds;
};

Status serialize_dns_a_query(
    uint16_t transaction_id,
    const char* name,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length);
Status parse_dns_a_response(
    const uint8_t* packet,
    size_t packet_length,
    uint16_t expected_transaction_id,
    DnsAnswer* out_answer);

} // namespace net

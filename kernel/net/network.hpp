#pragma once

#include <stddef.h>
#include <stdint.h>

namespace net {

static constexpr size_t MAC_ADDRESS_LENGTH = 6;
static constexpr size_t IPV4_ADDRESS_LENGTH = 4;
static constexpr size_t ETHERNET_HEADER_SIZE = 14;
static constexpr size_t ETHERNET_MTU = 1500;
static constexpr size_t ETHERNET_MAX_FRAME_SIZE =
    ETHERNET_HEADER_SIZE + ETHERNET_MTU;
static constexpr size_t ARP_PACKET_SIZE = 28;
static constexpr size_t IPV4_MIN_HEADER_SIZE = 20;
static constexpr size_t IPV4_MAX_HEADER_SIZE = 60;
static constexpr size_t ICMP_ECHO_HEADER_SIZE = 8;
static constexpr size_t ICMP_ECHO_MAX_PAYLOAD =
    ETHERNET_MTU - IPV4_MIN_HEADER_SIZE - ICMP_ECHO_HEADER_SIZE;
static constexpr size_t LOOPBACK_QUEUE_DEPTH = 8;
static constexpr size_t NEIGHBOR_TABLE_CAPACITY = 16;

/*
 * A normal Ethernet/TCP peer may deliver close to one full MTU in a single
 * TCP segment. The old 512-byte transport inbox caused perfectly valid HTTP
 * responses to fail with BufferTooSmall, which surfaced in Kurogane Web as
 * KU_STATUS_OUT_OF_RANGE (-2). Keep one full Ethernet MTU of transport
 * payload capacity so HTTP/DNS/TCP receive paths do not reject normal frames.
 */
static constexpr size_t TRANSPORT_INBOX_CAPACITY = ETHERNET_MTU;

static constexpr uint16_t ETHER_TYPE_IPV4 = UINT16_C(0x0800);
static constexpr uint16_t ETHER_TYPE_ARP = UINT16_C(0x0806);
static constexpr uint8_t IPV4_PROTOCOL_ICMP = 1;

enum class Status : uint8_t {
    Ok = 0,
    NotInitialized,
    NotConfigured,
    InvalidArgument,
    InvalidConfiguration,
    WouldBlock,
    QueueFull,
    BufferTooSmall,
    FrameTooShort,
    FrameTooLarge,
    PayloadTooLarge,
    MalformedPacket,
    ChecksumMismatch,
    UnsupportedProtocol,
    UnsupportedFragment,
    NotForUs,
    NoRoute,
    NeighborResolutionPending,
    IterationStopped,
    InterfaceError
};

struct MacAddress {
    uint8_t bytes[MAC_ADDRESS_LENGTH];
};

struct IPv4Address {
    uint8_t bytes[IPV4_ADDRESS_LENGTH];
};

bool mac_equal(const MacAddress& left, const MacAddress& right);
bool mac_is_zero(const MacAddress& address);
bool mac_is_broadcast(const MacAddress& address);
bool mac_is_multicast(const MacAddress& address);
bool ipv4_equal(const IPv4Address& left, const IPv4Address& right);
bool ipv4_is_zero(const IPv4Address& address);
bool ipv4_is_multicast(const IPv4Address& address);
bool ipv4_is_limited_broadcast(const IPv4Address& address);

uint16_t read_be16(const uint8_t* data);
uint32_t read_be32(const uint8_t* data);
void write_be16(uint8_t* data, uint16_t value);
void write_be32(uint8_t* data, uint32_t value);
uint16_t internet_checksum(const uint8_t* data, size_t length);

struct EthernetFrameView {
    MacAddress destination;
    MacAddress source;
    uint16_t ether_type;
    const uint8_t* payload;
    size_t payload_length;
};

Status parse_ethernet(
    const uint8_t* frame,
    size_t frame_length,
    EthernetFrameView* out_frame
);
Status serialize_ethernet(
    const MacAddress& destination,
    const MacAddress& source,
    uint16_t ether_type,
    const uint8_t* payload,
    size_t payload_length,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length
);

enum class ArpOperation : uint16_t {
    Request = 1,
    Reply = 2
};

struct ArpPacket {
    ArpOperation operation;
    MacAddress sender_mac;
    IPv4Address sender_ip;
    MacAddress target_mac;
    IPv4Address target_ip;
};

Status parse_arp(
    const uint8_t* packet,
    size_t packet_length,
    ArpPacket* out_packet
);
Status serialize_arp(
    const ArpPacket& packet,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length
);

struct IPv4PacketView {
    IPv4Address source;
    IPv4Address destination;
    uint8_t protocol;
    uint8_t ttl;
    bool dont_fragment;
    const uint8_t* payload;
    size_t payload_length;
    size_t header_length;
};

struct IPv4Header {
    IPv4Address source;
    IPv4Address destination;
    uint8_t protocol;
    uint8_t ttl;
    uint16_t identification;
    bool dont_fragment;
};

Status parse_ipv4(
    const uint8_t* packet,
    size_t packet_length,
    IPv4PacketView* out_packet
);
Status serialize_ipv4(
    const IPv4Header& header,
    const uint8_t* payload,
    size_t payload_length,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length
);

enum class IcmpEchoType : uint8_t {
    Reply = 0,
    Request = 8
};

struct IcmpEchoView {
    IcmpEchoType type;
    uint16_t identifier;
    uint16_t sequence;
    const uint8_t* payload;
    size_t payload_length;
};

Status parse_icmp_echo(
    const uint8_t* packet,
    size_t packet_length,
    IcmpEchoView* out_echo
);
Status serialize_icmp_echo(
    IcmpEchoType type,
    uint16_t identifier,
    uint16_t sequence,
    const uint8_t* payload,
    size_t payload_length,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length
);

struct IPv4Config {
    IPv4Address address;
    IPv4Address subnet_mask;
    IPv4Address gateway;
};

struct NeighborEntry {
    IPv4Address ip;
    MacAddress mac;
    bool valid;
};

struct NetworkStats {
    uint64_t frames_received;
    uint64_t frames_transmitted;
    uint64_t bytes_received;
    uint64_t bytes_transmitted;
    uint64_t dropped_frames;
};

struct EthernetInterface {
    void* context;
    MacAddress mac;
    Status (*send)(void* context, const uint8_t* frame, size_t length);
    Status (*receive)(void* context, uint8_t* frame, size_t capacity, size_t* out_length);
};

struct UdpDatagram {
    IPv4Address source;
    IPv4Address destination;
    uint16_t source_port;
    uint16_t destination_port;
    uint8_t payload[TRANSPORT_INBOX_CAPACITY];
    size_t payload_length;
};

struct TcpSegment {
    IPv4Address source;
    IPv4Address destination;
    uint16_t source_port;
    uint16_t destination_port;
    uint32_t sequence;
    uint32_t acknowledgement;
    uint8_t flags;
    uint16_t window;
    uint8_t payload[TRANSPORT_INBOX_CAPACITY];
    size_t payload_length;
};

struct PingReply {
    IPv4Address source;
    uint16_t identifier;
    uint16_t sequence;
    uint8_t payload[ICMP_ECHO_MAX_PAYLOAD];
    size_t payload_length;
    bool valid;
};

struct NetworkStack {
    EthernetInterface* interface;
    IPv4Config config;
    NeighborEntry neighbors[NEIGHBOR_TABLE_CAPACITY];
    UdpDatagram udp_inbox;
    TcpSegment tcp_inbox;
    PingReply last_ping;
    NetworkStats stats;
    uint16_t next_ipv4_identification;
    bool initialized;
    bool configured;
};

struct LoopbackInterface {
    EthernetInterface interface;
    uint8_t queue[LOOPBACK_QUEUE_DEPTH][ETHERNET_MAX_FRAME_SIZE];
    size_t lengths[LOOPBACK_QUEUE_DEPTH];
    size_t head;
    size_t tail;
    size_t count;
};

Status initialize_loopback(LoopbackInterface* loopback, const MacAddress& mac);
Status initialize_stack(NetworkStack* stack, EthernetInterface* interface);
Status configure_ipv4(NetworkStack* stack, const IPv4Config& config);
Status poll(NetworkStack* stack, size_t budget, size_t* processed = nullptr);
Status send_ping(
    NetworkStack* stack,
    const IPv4Address& destination,
    uint16_t identifier,
    uint16_t sequence,
    const uint8_t* payload,
    size_t payload_length
);
Status get_last_ping_reply(const NetworkStack* stack, PingReply* out_reply);
Status get_stats(const NetworkStack* stack, NetworkStats* out_stats);
Status lookup_neighbor(
    const NetworkStack* stack,
    const IPv4Address& ip,
    NeighborEntry* out_entry
);
using NeighborCallback = bool (*)(const NeighborEntry& entry, void* context);
Status list_neighbors(
    const NetworkStack* stack,
    NeighborCallback callback,
    void* context
);
Status send_udp(
    NetworkStack* stack,
    const IPv4Address& destination,
    uint16_t source_port,
    uint16_t destination_port,
    const uint8_t* payload,
    size_t payload_length
);
Status take_udp_datagram(NetworkStack* stack, UdpDatagram* output);
Status send_tcp(
    NetworkStack* stack,
    const IPv4Address& destination,
    uint16_t source_port,
    uint16_t destination_port,
    uint32_t sequence,
    uint32_t acknowledgement,
    uint8_t flags,
    uint16_t window,
    const uint8_t* payload,
    size_t payload_length
);
Status take_tcp_segment(NetworkStack* stack, TcpSegment* output);

const char* status_message(Status status);

} // namespace net

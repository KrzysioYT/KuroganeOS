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
static constexpr size_t TRANSPORT_INBOX_CAPACITY = 512;

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
    uint16_t identification;
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

struct NetworkInterface;
typedef Status (*InterfaceTransmit)(
    void* context,
    const uint8_t* frame,
    size_t frame_length
);
typedef Status (*InterfaceReceive)(
    void* context,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length
);

struct NetworkInterface {
    void* context;
    InterfaceTransmit transmit;
    InterfaceReceive receive;
    MacAddress hardware_address;
    size_t mtu;
};

Status interface_transmit(
    NetworkInterface* interface,
    const uint8_t* frame,
    size_t frame_length
);
Status interface_receive(
    NetworkInterface* interface,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length
);

struct LoopbackInterface {
    NetworkInterface interface;
    uint8_t frames[LOOPBACK_QUEUE_DEPTH][ETHERNET_MAX_FRAME_SIZE];
    uint16_t lengths[LOOPBACK_QUEUE_DEPTH];
    size_t head;
    size_t tail;
    size_t count;
    uint64_t transmitted_frames;
    uint64_t received_frames;
    uint64_t dropped_frames;
    bool initialized;
};

Status initialize_loopback(
    LoopbackInterface* loopback,
    const MacAddress& hardware_address
);
size_t loopback_queued_frames(const LoopbackInterface* loopback);

struct IPv4Config {
    IPv4Address address;
    IPv4Address netmask;
    IPv4Address gateway;
};

enum class NeighborState : uint8_t {
    Empty = 0,
    Reachable
};

struct NeighborEntry {
    NeighborState state;
    IPv4Address ip;
    MacAddress mac;
    uint64_t update_sequence;
};

struct NetworkStats {
    uint64_t frames_received;
    uint64_t frames_transmitted;
    uint64_t bytes_received;
    uint64_t bytes_transmitted;
    uint64_t dropped_frames;
    uint64_t parse_errors;
    uint64_t checksum_errors;
    uint64_t unsupported_packets;
    uint64_t interface_errors;
    uint64_t arp_received;
    uint64_t arp_transmitted;
    uint64_t ipv4_received;
    uint64_t ipv4_transmitted;
    uint64_t icmp_received;
    uint64_t icmp_transmitted;
    uint64_t echo_requests_sent;
    uint64_t echo_requests_received;
    uint64_t echo_replies_sent;
    uint64_t echo_replies_received;
    uint64_t udp_received;
    uint64_t udp_transmitted;
    uint64_t tcp_received;
    uint64_t tcp_transmitted;
    uint64_t neighbor_updates;
    uint64_t neighbor_evictions;
    uint64_t arp_resolution_requests;
};

struct PingReply {
    bool valid;
    IPv4Address source;
    uint16_t identifier;
    uint16_t sequence;
    size_t payload_length;
};

struct UdpDatagram {
    bool valid;
    IPv4Address source;
    IPv4Address destination;
    uint16_t source_port;
    uint16_t destination_port;
    uint8_t payload[TRANSPORT_INBOX_CAPACITY];
    size_t payload_length;
};

struct TcpSegment {
    bool valid;
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

struct NetworkStack {
    NetworkInterface* interface;
    IPv4Config config;
    NeighborEntry neighbors[NEIGHBOR_TABLE_CAPACITY];
    NetworkStats stats;
    PingReply last_ping_reply;
    UdpDatagram last_udp_datagram;
    TcpSegment last_tcp_segment;
    uint8_t receive_buffer[ETHERNET_MAX_FRAME_SIZE];
    uint8_t transmit_buffer[ETHERNET_MAX_FRAME_SIZE];
    uint16_t next_identification;
    size_t neighbor_eviction_cursor;
    uint64_t neighbor_update_sequence;
    bool initialized;
    bool ipv4_configured;
};

typedef bool (*NeighborCallback)(const NeighborEntry* entry, void* context);

Status initialize_stack(NetworkStack* stack, NetworkInterface* interface);
Status configure_ipv4(NetworkStack* stack, const IPv4Config& config);
Status receive(NetworkStack* stack, const uint8_t* frame, size_t frame_length);
Status poll(NetworkStack* stack, size_t budget, size_t* out_processed = nullptr);
Status send_ping(
    NetworkStack* stack,
    const IPv4Address& destination,
    uint16_t identifier,
    uint16_t sequence,
    const uint8_t* payload,
    size_t payload_length
);
Status send_udp(
    NetworkStack* stack,
    const IPv4Address& destination,
    uint16_t source_port,
    uint16_t destination_port,
    const uint8_t* payload,
    size_t payload_length
);
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
Status lookup_neighbor(
    const NetworkStack* stack,
    const IPv4Address& address,
    NeighborEntry* out_entry
);
Status list_neighbors(
    const NetworkStack* stack,
    NeighborCallback callback,
    void* context
);
Status get_stats(const NetworkStack* stack, NetworkStats* out_stats);
Status get_last_ping_reply(const NetworkStack* stack, PingReply* out_reply);
Status take_udp_datagram(NetworkStack* stack, UdpDatagram* out_datagram);
Status take_tcp_segment(NetworkStack* stack, TcpSegment* out_segment);
const char* status_message(Status status);

} // namespace net

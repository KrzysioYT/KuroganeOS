#include "dhcp.hpp"

#include "protocols.hpp"

namespace net::dhcp {
namespace {

constexpr MacAddress BROADCAST_MAC = {
    {0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU}
};
constexpr IPv4Address ZERO_IP = {{0U, 0U, 0U, 0U}};
constexpr IPv4Address BROADCAST_IP = {{255U, 255U, 255U, 255U}};
constexpr uint16_t CLIENT_PORT = 68U;
constexpr uint16_t SERVER_PORT = 67U;
constexpr size_t WAIT_BUDGET = 10000000U;
constexpr size_t ATTEMPTS = 3U;

void relax() { __asm__ volatile("pause" : : : "memory"); }

uint32_t transaction_for(const MacAddress& mac) {
    uint32_t value = UINT32_C(0x4b550000);
    for (size_t index = 0U; index < MAC_ADDRESS_LENGTH; ++index) {
        value = (value << 5U) ^ (value >> 2U) ^ mac.bytes[index];
    }
    return value == 0U ? UINT32_C(0x4b55524f) : value;
}

Status transmit_message(
    NetworkInterface* interface,
    const uint8_t* message,
    size_t message_length,
    uint16_t identification) {
    uint8_t frame[ETHERNET_MAX_FRAME_SIZE]{};
    constexpr size_t udp_offset = ETHERNET_HEADER_SIZE + IPV4_MIN_HEADER_SIZE;
    if (message_length > UDP_MAX_PAYLOAD) return Status::PayloadTooLarge;
    size_t udp_length = 0U;
    Status status = serialize_udp(
        ZERO_IP, BROADCAST_IP, CLIENT_PORT, SERVER_PORT,
        message, message_length,
        frame + udp_offset, sizeof(frame) - udp_offset, &udp_length);
    if (status != Status::Ok) return status;
    const IPv4Header header = {
        ZERO_IP, BROADCAST_IP, IPV4_PROTOCOL_UDP,
        64U, identification, true
    };
    size_t ipv4_length = 0U;
    status = serialize_ipv4(
        header, frame + udp_offset, udp_length,
        frame + ETHERNET_HEADER_SIZE, ETHERNET_MTU, &ipv4_length);
    if (status != Status::Ok) return status;
    size_t frame_length = 0U;
    status = serialize_ethernet(
        BROADCAST_MAC, interface->hardware_address, ETHER_TYPE_IPV4,
        frame + ETHERNET_HEADER_SIZE, ipv4_length,
        frame, sizeof(frame), &frame_length);
    if (status != Status::Ok) return status;
    return interface_transmit(interface, frame, frame_length);
}

Status wait_for_reply(
    NetworkInterface* interface,
    uint32_t transaction_id,
    DhcpMessageType expected,
    DhcpMessageView* out_message) {
    uint8_t frame[ETHERNET_MAX_FRAME_SIZE]{};
    for (size_t spin = 0U; spin < WAIT_BUDGET; ++spin) {
        size_t frame_length = 0U;
        Status status = interface_receive(
            interface, frame, sizeof(frame), &frame_length);
        if (status == Status::WouldBlock) {
            relax();
            continue;
        }
        if (status != Status::Ok) return status;
        EthernetFrameView ethernet{};
        status = parse_ethernet(frame, frame_length, &ethernet);
        if (status != Status::Ok || ethernet.ether_type != ETHER_TYPE_IPV4) {
            continue;
        }
        IPv4PacketView ipv4{};
        status = parse_ipv4(ethernet.payload, ethernet.payload_length, &ipv4);
        if (status != Status::Ok || ipv4.protocol != IPV4_PROTOCOL_UDP) continue;
        UdpDatagramView udp{};
        status = parse_udp(
            ipv4.source, ipv4.destination,
            ipv4.payload, ipv4.payload_length, &udp);
        if (status != Status::Ok || udp.source_port != SERVER_PORT ||
            udp.destination_port != CLIENT_PORT) continue;
        DhcpMessageView message{};
        status = parse_dhcp_reply(
            udp.payload, udp.payload_length,
            transaction_id, interface->hardware_address, &message);
        if (status != Status::Ok) continue;
        if (message.type == DhcpMessageType::Nak) {
            return Status::InvalidConfiguration;
        }
        if (message.type == expected) {
            *out_message = message;
            return Status::Ok;
        }
    }
    return Status::WouldBlock;
}

} // namespace

Status acquire(NetworkInterface* interface, Lease* out_lease) {
    if (interface == nullptr || out_lease == nullptr ||
        interface->transmit == nullptr || interface->receive == nullptr) {
        return Status::InvalidArgument;
    }
    const uint32_t transaction = transaction_for(interface->hardware_address);
    uint8_t message[576]{};
    DhcpMessageView offer{};
    bool offered = false;
    for (size_t attempt = 0U; attempt < ATTEMPTS && !offered; ++attempt) {
        size_t length = 0U;
        Status status = serialize_dhcp_discover(
            transaction, interface->hardware_address,
            message, sizeof(message), &length);
        if (status != Status::Ok) return status;
        status = transmit_message(
            interface, message, length, static_cast<uint16_t>(attempt + 1U));
        if (status != Status::Ok) return status;
        status = wait_for_reply(
            interface, transaction, DhcpMessageType::Offer, &offer);
        offered = status == Status::Ok;
        if (!offered && status != Status::WouldBlock) return status;
    }
    if (!offered || !offer.has_server_identifier) return Status::WouldBlock;

    DhcpMessageView acknowledgement{};
    bool acknowledged = false;
    for (size_t attempt = 0U; attempt < ATTEMPTS && !acknowledged; ++attempt) {
        size_t length = 0U;
        Status status = serialize_dhcp_request(
            transaction, interface->hardware_address,
            offer.offered_address, offer.server_identifier,
            message, sizeof(message), &length);
        if (status != Status::Ok) return status;
        status = transmit_message(
            interface, message, length, static_cast<uint16_t>(attempt + 4U));
        if (status != Status::Ok) return status;
        status = wait_for_reply(
            interface, transaction, DhcpMessageType::Ack, &acknowledgement);
        acknowledged = status == Status::Ok;
        if (!acknowledged && status != Status::WouldBlock) return status;
    }
    if (!acknowledged) return Status::WouldBlock;
    if (!acknowledgement.has_netmask || !acknowledgement.has_gateway ||
        !acknowledgement.has_dns_server) return Status::InvalidConfiguration;
    Lease lease{};
    lease.configuration = {
        acknowledgement.offered_address,
        acknowledgement.netmask,
        acknowledgement.gateway
    };
    lease.dns_server = acknowledgement.dns_server;
    lease.server_identifier = acknowledgement.has_server_identifier
        ? acknowledgement.server_identifier
        : offer.server_identifier;
    lease.lease_seconds = acknowledgement.lease_seconds;
    *out_lease = lease;
    return Status::Ok;
}

} // namespace net::dhcp

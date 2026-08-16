#include "../kernel/net/protocols.hpp"

#include <cstddef>
#include <cstdint>

namespace {

void copy_bytes(void* destination, const void* source, size_t count) {
    auto* output = static_cast<uint8_t*>(destination);
    const auto* input = static_cast<const uint8_t*>(source);
    for (size_t index = 0U; index < count; ++index) output[index] = input[index];
}

bool equal_bytes(const uint8_t* left, const uint8_t* right, size_t count) {
    for (size_t index = 0U; index < count; ++index) {
        if (left[index] != right[index]) return false;
    }
    return true;
}

} // namespace

int main() {
    const net::IPv4Address source = {{10U, 0U, 2U, 15U}};
    const net::IPv4Address destination = {{10U, 0U, 2U, 3U}};
    const uint8_t payload[] = {1U, 3U, 3U, 7U, 9U};
    uint8_t packet[1024]{};
    size_t length = 0U;
    if (net::serialize_udp(
            source, destination, 49152U, 53U,
            payload, sizeof(payload), packet, sizeof(packet), &length) !=
            net::Status::Ok || length != net::UDP_HEADER_SIZE + sizeof(payload)) {
        return 1;
    }
    net::UdpDatagramView udp{};
    if (net::parse_udp(source, destination, packet, length, &udp) !=
            net::Status::Ok || udp.source_port != 49152U ||
        udp.destination_port != 53U || udp.payload_length != sizeof(payload) ||
        !equal_bytes(udp.payload, payload, sizeof(payload))) {
        return 2;
    }
    packet[length - 1U] ^= 1U;
    if (net::parse_udp(source, destination, packet, length, &udp) !=
        net::Status::ChecksumMismatch) return 3;
    packet[length - 1U] ^= 1U;

    if (net::serialize_tcp(
            source, destination, 50000U, 80U,
            UINT32_C(0x12345678), UINT32_C(0x87654321),
            net::TcpPsh | net::TcpAck, 32768U,
            payload, sizeof(payload), packet, sizeof(packet), &length) !=
            net::Status::Ok) return 4;
    net::TcpSegmentView tcp{};
    if (net::parse_tcp(source, destination, packet, length, &tcp) !=
            net::Status::Ok || tcp.source_port != 50000U ||
        tcp.destination_port != 80U || tcp.sequence != UINT32_C(0x12345678) ||
        tcp.acknowledgement != UINT32_C(0x87654321) ||
        tcp.flags != (net::TcpPsh | net::TcpAck) ||
        tcp.payload_length != sizeof(payload)) return 5;

    const net::MacAddress mac = {{0x52U, 0x54U, 0U, 0x4bU, 0x55U, 1U}};
    constexpr uint32_t xid = UINT32_C(0x4b55524f);
    if (net::serialize_dhcp_discover(
            xid, mac, packet, sizeof(packet), &length) != net::Status::Ok ||
        length <= 240U || net::read_be32(packet + 236U) != UINT32_C(0x63825363)) {
        return 6;
    }
    for (size_t index = 0U; index < sizeof(packet); ++index) packet[index] = 0U;
    packet[0] = 2U;
    packet[1] = 1U;
    packet[2] = 6U;
    net::write_be32(packet + 4U, xid);
    const net::IPv4Address offered = {{10U, 0U, 2U, 15U}};
    const net::IPv4Address gateway = {{10U, 0U, 2U, 2U}};
    const net::IPv4Address dns = {{10U, 0U, 2U, 3U}};
    const net::IPv4Address mask = {{255U, 255U, 255U, 0U}};
    copy_bytes(packet + 16U, offered.bytes, 4U);
    copy_bytes(packet + 28U, mac.bytes, 6U);
    net::write_be32(packet + 236U, UINT32_C(0x63825363));
    size_t cursor = 240U;
    auto option = [&](uint8_t code, const uint8_t* value, size_t count) {
        packet[cursor++] = code;
        packet[cursor++] = static_cast<uint8_t>(count);
        copy_bytes(packet + cursor, value, count);
        cursor += count;
    };
    const uint8_t ack = static_cast<uint8_t>(net::DhcpMessageType::Ack);
    const uint8_t lease[] = {0U, 0U, 0x0eU, 0x10U};
    option(53U, &ack, 1U);
    option(54U, gateway.bytes, 4U);
    option(1U, mask.bytes, 4U);
    option(3U, gateway.bytes, 4U);
    option(6U, dns.bytes, 4U);
    option(51U, lease, 4U);
    packet[cursor++] = 255U;
    net::DhcpMessageView dhcp{};
    if (net::parse_dhcp_reply(packet, cursor, xid, mac, &dhcp) !=
            net::Status::Ok || dhcp.type != net::DhcpMessageType::Ack ||
        !net::ipv4_equal(dhcp.offered_address, offered) ||
        !net::ipv4_equal(dhcp.netmask, mask) ||
        !net::ipv4_equal(dhcp.gateway, gateway) ||
        !net::ipv4_equal(dhcp.dns_server, dns) || dhcp.lease_seconds != 3600U) {
        return 7;
    }

    uint8_t query[512]{};
    size_t query_length = 0U;
    if (net::serialize_dns_a_query(
            UINT16_C(0x1234), "example.com",
            query, sizeof(query), &query_length) != net::Status::Ok) return 8;
    uint8_t response[512]{};
    copy_bytes(response, query, query_length);
    net::write_be16(response + 2U, UINT16_C(0x8180));
    net::write_be16(response + 6U, 1U);
    cursor = query_length;
    response[cursor++] = UINT8_C(0xc0);
    response[cursor++] = 12U;
    net::write_be16(response + cursor, 1U);
    net::write_be16(response + cursor + 2U, 1U);
    net::write_be32(response + cursor + 4U, 60U);
    net::write_be16(response + cursor + 8U, 4U);
    cursor += 10U;
    const uint8_t answer_address[] = {93U, 184U, 216U, 34U};
    copy_bytes(response + cursor, answer_address, 4U);
    cursor += 4U;
    net::DnsAnswer answer{};
    if (net::parse_dns_a_response(
            response, cursor, UINT16_C(0x1234), &answer) != net::Status::Ok ||
        !equal_bytes(answer.address.bytes, answer_address, 4U) ||
        answer.ttl_seconds != 60U) return 9;
    if (net::parse_dns_a_response(
            response, cursor, UINT16_C(0x9999), &answer) !=
        net::Status::NotForUs) return 10;
    return 0;
}

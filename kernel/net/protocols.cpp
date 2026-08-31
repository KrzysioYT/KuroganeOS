#include "protocols.hpp"

namespace net {
namespace {

constexpr size_t DHCP_FIXED_SIZE = 240U;
constexpr uint32_t DHCP_COOKIE = UINT32_C(0x63825363);
constexpr uint8_t DHCP_OPTION_SUBNET = 1U;
constexpr uint8_t DHCP_OPTION_ROUTER = 3U;
constexpr uint8_t DHCP_OPTION_DNS = 6U;
constexpr uint8_t DHCP_OPTION_REQUESTED_IP = 50U;
constexpr uint8_t DHCP_OPTION_LEASE = 51U;
constexpr uint8_t DHCP_OPTION_TYPE = 53U;
constexpr uint8_t DHCP_OPTION_SERVER = 54U;
constexpr uint8_t DHCP_OPTION_PARAMETERS = 55U;
constexpr uint8_t DHCP_OPTION_CLIENT_ID = 61U;
constexpr uint8_t DHCP_OPTION_END = 255U;
constexpr uint8_t DHCP_OPTION_PAD = 0U;

void clear_bytes(void* destination, size_t count) {
    auto* bytes = static_cast<uint8_t*>(destination);
    for (size_t index = 0U; index < count; ++index) bytes[index] = 0U;
}

void copy_bytes(void* destination, const void* source, size_t count) {
    auto* output = static_cast<uint8_t*>(destination);
    const auto* input = static_cast<const uint8_t*>(source);
    for (size_t index = 0U; index < count; ++index) output[index] = input[index];
}

uint32_t checksum_add(uint32_t sum, const uint8_t* bytes, size_t count) {
    size_t index = 0U;
    while (index + 1U < count) {
        sum += static_cast<uint32_t>(
            (static_cast<uint16_t>(bytes[index]) << 8U) |
            bytes[index + 1U]);
        index += 2U;
    }
    if (index < count) sum += static_cast<uint32_t>(bytes[index]) << 8U;
    return sum;
}

uint16_t checksum_finish(uint32_t sum) {
    while ((sum >> 16U) != 0U) sum = (sum & UINT32_C(0xffff)) + (sum >> 16U);
    return static_cast<uint16_t>(~sum);
}

uint16_t transport_checksum(
    const IPv4Address& source,
    const IPv4Address& destination,
    uint8_t protocol,
    const uint8_t* packet,
    size_t length) {
    uint32_t sum = 0U;
    sum = checksum_add(sum, source.bytes, sizeof(source.bytes));
    sum = checksum_add(sum, destination.bytes, sizeof(destination.bytes));
    const uint8_t pseudo[] = {
        0U, protocol,
        static_cast<uint8_t>(length >> 8U),
        static_cast<uint8_t>(length)
    };
    sum = checksum_add(sum, pseudo, sizeof(pseudo));
    sum = checksum_add(sum, packet, length);
    return checksum_finish(sum);
}

bool valid_port(uint16_t port) { return port != 0U; }

bool append_option(
    uint8_t* output,
    size_t capacity,
    size_t* cursor,
    uint8_t type,
    const uint8_t* value,
    size_t value_length) {
    if (value_length > UINT8_MAX || *cursor > capacity ||
        capacity - *cursor < value_length + 2U) return false;
    output[(*cursor)++] = type;
    output[(*cursor)++] = static_cast<uint8_t>(value_length);
    copy_bytes(output + *cursor, value, value_length);
    *cursor += value_length;
    return true;
}

Status initialize_dhcp_request(
    uint32_t transaction_id,
    const MacAddress& client,
    uint8_t* output,
    size_t output_capacity,
    size_t* cursor) {
    if (output == nullptr || cursor == nullptr) return Status::InvalidArgument;
    if (output_capacity < DHCP_FIXED_SIZE + 1U) {
        *cursor = DHCP_FIXED_SIZE + 1U;
        return Status::BufferTooSmall;
    }
    clear_bytes(output, output_capacity);
    output[0] = 1U;
    output[1] = 1U;
    output[2] = static_cast<uint8_t>(MAC_ADDRESS_LENGTH);
    write_be32(output + 4U, transaction_id);
    write_be16(output + 10U, UINT16_C(0x8000));
    copy_bytes(output + 28U, client.bytes, MAC_ADDRESS_LENGTH);
    write_be32(output + 236U, DHCP_COOKIE);
    *cursor = DHCP_FIXED_SIZE;
    return Status::Ok;
}

bool ipv4_option(
    const uint8_t* value,
    size_t length,
    IPv4Address* output) {
    if (length < IPV4_ADDRESS_LENGTH || output == nullptr) return false;
    copy_bytes(output->bytes, value, IPV4_ADDRESS_LENGTH);
    return true;
}

Status skip_dns_name(
    const uint8_t* packet,
    size_t packet_length,
    size_t* cursor) {
    if (packet == nullptr || cursor == nullptr || *cursor >= packet_length) {
        return Status::MalformedPacket;
    }
    size_t position = *cursor;
    size_t labels = 0U;
    while (position < packet_length) {
        const uint8_t length = packet[position++];
        if (length == 0U) {
            *cursor = position;
            return Status::Ok;
        }
        if ((length & UINT8_C(0xc0)) == UINT8_C(0xc0)) {
            if (position >= packet_length) return Status::MalformedPacket;
            *cursor = position + 1U;
            return Status::Ok;
        }
        if ((length & UINT8_C(0xc0)) != 0U || length > 63U ||
            position > packet_length || packet_length - position < length) {
            return Status::MalformedPacket;
        }
        position += length;
        if (++labels > 127U) return Status::MalformedPacket;
    }
    return Status::MalformedPacket;
}

} // namespace

Status parse_udp(
    const IPv4Address& source,
    const IPv4Address& destination,
    const uint8_t* packet,
    size_t packet_length,
    UdpDatagramView* out_datagram) {
    if (packet == nullptr || out_datagram == nullptr) return Status::InvalidArgument;
    if (packet_length < UDP_HEADER_SIZE) return Status::FrameTooShort;
    const size_t declared = read_be16(packet + 4U);
    if (declared < UDP_HEADER_SIZE || declared > packet_length) {
        return Status::MalformedPacket;
    }
    const uint16_t source_port = read_be16(packet);
    const uint16_t destination_port = read_be16(packet + 2U);
    if (!valid_port(source_port) || !valid_port(destination_port)) {
        return Status::MalformedPacket;
    }
    if (read_be16(packet + 6U) != 0U &&
        transport_checksum(source, destination, IPV4_PROTOCOL_UDP,
                           packet, declared) != 0U) {
        return Status::ChecksumMismatch;
    }
    *out_datagram = {
        source_port,
        destination_port,
        packet + UDP_HEADER_SIZE,
        declared - UDP_HEADER_SIZE
    };
    return Status::Ok;
}

Status serialize_udp(
    const IPv4Address& source,
    const IPv4Address& destination,
    uint16_t source_port,
    uint16_t destination_port,
    const uint8_t* payload,
    size_t payload_length,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length) {
    if (out_length != nullptr) *out_length = 0U;
    if (output == nullptr || out_length == nullptr ||
        (payload == nullptr && payload_length != 0U) ||
        !valid_port(source_port) || !valid_port(destination_port)) {
        return Status::InvalidArgument;
    }
    if (payload_length > UDP_MAX_PAYLOAD) return Status::PayloadTooLarge;
    const size_t required = UDP_HEADER_SIZE + payload_length;
    if (output_capacity < required) {
        *out_length = required;
        return Status::BufferTooSmall;
    }
    write_be16(output, source_port);
    write_be16(output + 2U, destination_port);
    write_be16(output + 4U, static_cast<uint16_t>(required));
    write_be16(output + 6U, 0U);
    copy_bytes(output + UDP_HEADER_SIZE, payload, payload_length);
    uint16_t checksum = transport_checksum(
        source, destination, IPV4_PROTOCOL_UDP, output, required);
    if (checksum == 0U) checksum = UINT16_MAX;
    write_be16(output + 6U, checksum);
    *out_length = required;
    return Status::Ok;
}

Status parse_tcp(
    const IPv4Address& source,
    const IPv4Address& destination,
    const uint8_t* packet,
    size_t packet_length,
    TcpSegmentView* out_segment) {
    if (packet == nullptr || out_segment == nullptr) return Status::InvalidArgument;
    if (packet_length < TCP_MIN_HEADER_SIZE) return Status::FrameTooShort;
    const size_t header_length = static_cast<size_t>(packet[12U] >> 4U) * 4U;
    if (header_length < TCP_MIN_HEADER_SIZE || header_length > packet_length) {
        return Status::MalformedPacket;
    }
    const uint16_t source_port = read_be16(packet);
    const uint16_t destination_port = read_be16(packet + 2U);
    if (!valid_port(source_port) || !valid_port(destination_port)) {
        return Status::MalformedPacket;
    }
    if (transport_checksum(source, destination, IPV4_PROTOCOL_TCP,
                           packet, packet_length) != 0U) {
        return Status::ChecksumMismatch;
    }
    *out_segment = {
        source_port,
        destination_port,
        read_be32(packet + 4U),
        read_be32(packet + 8U),
        packet[13U],
        read_be16(packet + 14U),
        packet + header_length,
        packet_length - header_length
    };
    return Status::Ok;
}

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
    size_t* out_length) {
    if (out_length != nullptr) *out_length = 0U;
    if (output == nullptr || out_length == nullptr ||
        (payload == nullptr && payload_length != 0U) ||
        !valid_port(source_port) || !valid_port(destination_port)) {
        return Status::InvalidArgument;
    }
    if (payload_length > TCP_MAX_PAYLOAD) return Status::PayloadTooLarge;
    const size_t required = TCP_MIN_HEADER_SIZE + payload_length;
    if (output_capacity < required) {
        *out_length = required;
        return Status::BufferTooSmall;
    }
    clear_bytes(output, TCP_MIN_HEADER_SIZE);
    write_be16(output, source_port);
    write_be16(output + 2U, destination_port);
    write_be32(output + 4U, sequence);
    write_be32(output + 8U, acknowledgement);
    output[12U] = 5U << 4U;
    output[13U] = flags;
    write_be16(output + 14U, window);
    copy_bytes(output + TCP_MIN_HEADER_SIZE, payload, payload_length);
    write_be16(output + 16U, transport_checksum(
        source, destination, IPV4_PROTOCOL_TCP, output, required));
    *out_length = required;
    return Status::Ok;
}

Status serialize_dhcp_discover(
    uint32_t transaction_id,
    const MacAddress& client,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length) {
    if (out_length != nullptr) *out_length = 0U;
    if (out_length == nullptr) return Status::InvalidArgument;
    size_t cursor = 0U;
    Status status = initialize_dhcp_request(
        transaction_id, client, output, output_capacity, &cursor);
    if (status != Status::Ok) return status;
    const uint8_t type = static_cast<uint8_t>(DhcpMessageType::Discover);
    const uint8_t parameters[] = {
        DHCP_OPTION_SUBNET, DHCP_OPTION_ROUTER,
        DHCP_OPTION_DNS, DHCP_OPTION_LEASE
    };
    uint8_t client_id[1U + MAC_ADDRESS_LENGTH] = {1U};
    copy_bytes(client_id + 1U, client.bytes, MAC_ADDRESS_LENGTH);
    if (!append_option(output, output_capacity, &cursor,
                       DHCP_OPTION_TYPE, &type, 1U) ||
        !append_option(output, output_capacity, &cursor,
                       DHCP_OPTION_CLIENT_ID, client_id, sizeof(client_id)) ||
        !append_option(output, output_capacity, &cursor,
                       DHCP_OPTION_PARAMETERS, parameters, sizeof(parameters)) ||
        cursor >= output_capacity) {
        return Status::BufferTooSmall;
    }
    output[cursor++] = DHCP_OPTION_END;
    *out_length = cursor;
    return Status::Ok;
}

Status serialize_dhcp_request(
    uint32_t transaction_id,
    const MacAddress& client,
    const IPv4Address& requested_address,
    const IPv4Address& server_identifier,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length) {
    if (out_length != nullptr) *out_length = 0U;
    if (out_length == nullptr) return Status::InvalidArgument;
    size_t cursor = 0U;
    Status status = initialize_dhcp_request(
        transaction_id, client, output, output_capacity, &cursor);
    if (status != Status::Ok) return status;
    const uint8_t type = static_cast<uint8_t>(DhcpMessageType::Request);
    const uint8_t parameters[] = {
        DHCP_OPTION_SUBNET, DHCP_OPTION_ROUTER,
        DHCP_OPTION_DNS, DHCP_OPTION_LEASE
    };
    uint8_t client_id[1U + MAC_ADDRESS_LENGTH] = {1U};
    copy_bytes(client_id + 1U, client.bytes, MAC_ADDRESS_LENGTH);
    if (!append_option(output, output_capacity, &cursor,
                       DHCP_OPTION_TYPE, &type, 1U) ||
        !append_option(output, output_capacity, &cursor,
                       DHCP_OPTION_REQUESTED_IP,
                       requested_address.bytes, IPV4_ADDRESS_LENGTH) ||
        !append_option(output, output_capacity, &cursor,
                       DHCP_OPTION_SERVER,
                       server_identifier.bytes, IPV4_ADDRESS_LENGTH) ||
        !append_option(output, output_capacity, &cursor,
                       DHCP_OPTION_CLIENT_ID, client_id, sizeof(client_id)) ||
        !append_option(output, output_capacity, &cursor,
                       DHCP_OPTION_PARAMETERS, parameters, sizeof(parameters)) ||
        cursor >= output_capacity) {
        return Status::BufferTooSmall;
    }
    output[cursor++] = DHCP_OPTION_END;
    *out_length = cursor;
    return Status::Ok;
}

Status parse_dhcp_reply(
    const uint8_t* packet,
    size_t packet_length,
    uint32_t expected_transaction_id,
    const MacAddress& expected_client,
    DhcpMessageView* out_message) {
    if (packet == nullptr || out_message == nullptr) return Status::InvalidArgument;
    if (packet_length < DHCP_FIXED_SIZE) return Status::FrameTooShort;
    if (packet[0] != 2U || packet[1] != 1U ||
        packet[2] != MAC_ADDRESS_LENGTH ||
        read_be32(packet + 4U) != expected_transaction_id ||
        read_be32(packet + 236U) != DHCP_COOKIE) {
        return Status::NotForUs;
    }
    for (size_t index = 0U; index < MAC_ADDRESS_LENGTH; ++index) {
        if (packet[28U + index] != expected_client.bytes[index]) {
            return Status::NotForUs;
        }
    }
    DhcpMessageView result{};
    result.transaction_id = expected_transaction_id;
    copy_bytes(result.offered_address.bytes, packet + 16U, IPV4_ADDRESS_LENGTH);
    bool has_type = false;
    size_t cursor = DHCP_FIXED_SIZE;
    while (cursor < packet_length) {
        const uint8_t option = packet[cursor++];
        if (option == DHCP_OPTION_END) break;
        if (option == DHCP_OPTION_PAD) continue;
        if (cursor >= packet_length) return Status::MalformedPacket;
        const size_t length = packet[cursor++];
        if (length > packet_length - cursor) return Status::MalformedPacket;
        const uint8_t* value = packet + cursor;
        if (option == DHCP_OPTION_TYPE) {
            if (length != 1U || value[0] < 1U || value[0] > 6U) {
                return Status::MalformedPacket;
            }
            result.type = static_cast<DhcpMessageType>(value[0]);
            has_type = true;
        } else if (option == DHCP_OPTION_SERVER) {
            result.has_server_identifier = ipv4_option(
                value, length, &result.server_identifier);
        } else if (option == DHCP_OPTION_SUBNET) {
            result.has_netmask = ipv4_option(value, length, &result.netmask);
        } else if (option == DHCP_OPTION_ROUTER) {
            result.has_gateway = ipv4_option(value, length, &result.gateway);
        } else if (option == DHCP_OPTION_DNS) {
            result.has_dns_server = ipv4_option(value, length, &result.dns_server);
        } else if (option == DHCP_OPTION_LEASE && length == 4U) {
            result.lease_seconds = read_be32(value);
        }
        cursor += length;
    }
    if (!has_type || ipv4_is_zero(result.offered_address)) {
        return Status::MalformedPacket;
    }
    *out_message = result;
    return Status::Ok;
}

Status serialize_dns_a_query(
    uint16_t transaction_id,
    const char* name,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length) {
    if (out_length != nullptr) *out_length = 0U;
    if (name == nullptr || output == nullptr || out_length == nullptr ||
        name[0] == '\0') return Status::InvalidArgument;
    if (output_capacity < 17U) return Status::BufferTooSmall;
    clear_bytes(output, output_capacity);
    write_be16(output, transaction_id);
    write_be16(output + 2U, UINT16_C(0x0100));
    write_be16(output + 4U, 1U);
    size_t cursor = 12U;
    size_t label_start = 0U;
    size_t name_length = 0U;
    while (name[name_length] != '\0') {
        if (++name_length > 253U) return Status::InvalidArgument;
    }
    while (label_start < name_length) {
        size_t label_end = label_start;
        while (label_end < name_length && name[label_end] != '.') ++label_end;
        const size_t label_length = label_end - label_start;
        if (label_length == 0U || label_length > 63U ||
            cursor + 1U + label_length > output_capacity) {
            return Status::InvalidArgument;
        }
        output[cursor++] = static_cast<uint8_t>(label_length);
        copy_bytes(output + cursor, name + label_start, label_length);
        cursor += label_length;
        label_start = label_end + 1U;
    }
    if (cursor + 5U > output_capacity) return Status::BufferTooSmall;
    output[cursor++] = 0U;
    write_be16(output + cursor, 1U);
    write_be16(output + cursor + 2U, 1U);
    cursor += 4U;
    *out_length = cursor;
    return Status::Ok;
}

Status parse_dns_a_response(
    const uint8_t* packet,
    size_t packet_length,
    uint16_t expected_transaction_id,
    DnsAnswer* out_answer) {
    if (packet == nullptr || out_answer == nullptr) return Status::InvalidArgument;
    if (packet_length < 12U) return Status::FrameTooShort;
    const uint16_t flags = read_be16(packet + 2U);
    if (read_be16(packet) != expected_transaction_id) return Status::NotForUs;
    if ((flags & UINT16_C(0x8000)) == 0U) return Status::MalformedPacket;
    const uint16_t response_code = flags & UINT16_C(0x000f);
    if (response_code == UINT16_C(3)) return Status::NameNotFound;
    if (response_code != 0U) return Status::InterfaceError;
    const size_t questions = read_be16(packet + 4U);
    const size_t answers = read_be16(packet + 6U);
    if (questions == 0U || answers == 0U) return Status::WouldBlock;
    size_t cursor = 12U;
    for (size_t index = 0U; index < questions; ++index) {
        Status status = skip_dns_name(packet, packet_length, &cursor);
        if (status != Status::Ok) return status;
        if (cursor > packet_length || packet_length - cursor < 4U) {
            return Status::MalformedPacket;
        }
        cursor += 4U;
    }
    for (size_t index = 0U; index < answers; ++index) {
        Status status = skip_dns_name(packet, packet_length, &cursor);
        if (status != Status::Ok) return status;
        if (cursor > packet_length || packet_length - cursor < 10U) {
            return Status::MalformedPacket;
        }
        const uint16_t type = read_be16(packet + cursor);
        const uint16_t record_class = read_be16(packet + cursor + 2U);
        const uint32_t ttl = read_be32(packet + cursor + 4U);
        const size_t data_length = read_be16(packet + cursor + 8U);
        cursor += 10U;
        if (data_length > packet_length - cursor) return Status::MalformedPacket;
        if (type == 1U && record_class == 1U && data_length == 4U) {
            DnsAnswer answer{};
            answer.transaction_id = expected_transaction_id;
            copy_bytes(answer.address.bytes, packet + cursor, 4U);
            answer.ttl_seconds = ttl;
            *out_answer = answer;
            return Status::Ok;
        }
        cursor += data_length;
    }
    return Status::UnsupportedProtocol;
}

} // namespace net

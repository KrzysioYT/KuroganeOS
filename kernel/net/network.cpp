#include "network.hpp"
#include "protocols.hpp"

namespace net {

namespace {

static const MacAddress BROADCAST_MAC = {
    {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}
};
static const MacAddress ZERO_MAC = {{0, 0, 0, 0, 0, 0}};

void clear_bytes(void* destination, size_t length) {
    uint8_t* output = static_cast<uint8_t*>(destination);
    for (size_t i = 0; i < length; ++i) {
        output[i] = 0;
    }
}

void copy_bytes(void* destination, const void* source, size_t length) {
    uint8_t* output = static_cast<uint8_t*>(destination);
    const uint8_t* input = static_cast<const uint8_t*>(source);
    if (output == input || length == 0) {
        return;
    }
    const uintptr_t output_address = reinterpret_cast<uintptr_t>(output);
    const uintptr_t input_address = reinterpret_cast<uintptr_t>(input);
    if (output_address < input_address) {
        for (size_t i = 0; i < length; ++i) {
            output[i] = input[i];
        }
    } else {
        for (size_t i = length; i > 0; --i) {
            output[i - 1] = input[i - 1];
        }
    }
}

bool ranges_overlap(
    const uint8_t* left,
    size_t left_length,
    const uint8_t* right,
    size_t right_length
) {
    if (left_length == 0 || right_length == 0) {
        return false;
    }
    const uintptr_t left_address = reinterpret_cast<uintptr_t>(left);
    const uintptr_t right_address = reinterpret_cast<uintptr_t>(right);
    if (left_address <= right_address) {
        return right_address - left_address < left_length;
    }
    return left_address - right_address < right_length;
}

void saturating_add(uint64_t& value, uint64_t increment) {
    value = UINT64_MAX - value < increment ? UINT64_MAX : value + increment;
}

void increment(uint64_t& value) {
    saturating_add(value, 1);
}

uint32_t ipv4_value(const IPv4Address& address) {
    return (static_cast<uint32_t>(address.bytes[0]) << 24) |
           (static_cast<uint32_t>(address.bytes[1]) << 16) |
           (static_cast<uint32_t>(address.bytes[2]) << 8) |
           static_cast<uint32_t>(address.bytes[3]);
}

bool mac_is_valid_unicast(const MacAddress& address) {
    return !mac_is_zero(address) &&
           !mac_is_broadcast(address) &&
           !mac_is_multicast(address);
}

bool ipv4_is_valid_unicast(const IPv4Address& address) {
    return !ipv4_is_zero(address) &&
           !ipv4_is_multicast(address) &&
           !ipv4_is_limited_broadcast(address);
}

bool netmask_is_contiguous(const IPv4Address& netmask) {
    const uint32_t mask = ipv4_value(netmask);
    const uint32_t inverse = ~mask;
    return (inverse & (inverse + 1)) == 0;
}

bool ipv4_on_same_network(
    const IPv4Address& left,
    const IPv4Address& right,
    const IPv4Address& netmask
) {
    const uint32_t mask = ipv4_value(netmask);
    return (ipv4_value(left) & mask) == (ipv4_value(right) & mask);
}

bool config_address_is_host(
    const IPv4Address& address,
    const IPv4Address& netmask
) {
    if (!ipv4_is_valid_unicast(address)) {
        return false;
    }
    const uint32_t host_mask = ~ipv4_value(netmask);
    if (host_mask <= 1) {
        return true;
    }
    const uint32_t host = ipv4_value(address) & host_mask;
    return host != 0 && host != host_mask;
}

Status loopback_transmit(
    void* context,
    const uint8_t* frame,
    size_t frame_length
) {
    LoopbackInterface* loopback = static_cast<LoopbackInterface*>(context);
    if (!loopback || !loopback->initialized) {
        return Status::NotInitialized;
    }
    if (!frame || frame_length == 0) {
        return Status::InvalidArgument;
    }
    if (frame_length > ETHERNET_MAX_FRAME_SIZE) {
        return Status::FrameTooLarge;
    }
    if (loopback->count == LOOPBACK_QUEUE_DEPTH) {
        increment(loopback->dropped_frames);
        return Status::QueueFull;
    }

    copy_bytes(loopback->frames[loopback->tail], frame, frame_length);
    loopback->lengths[loopback->tail] = static_cast<uint16_t>(frame_length);
    loopback->tail = (loopback->tail + 1) % LOOPBACK_QUEUE_DEPTH;
    ++loopback->count;
    increment(loopback->transmitted_frames);
    return Status::Ok;
}

Status loopback_receive(
    void* context,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length
) {
    if (out_length) {
        *out_length = 0;
    }
    LoopbackInterface* loopback = static_cast<LoopbackInterface*>(context);
    if (!loopback || !loopback->initialized) {
        return Status::NotInitialized;
    }
    if (!out_length) {
        return Status::InvalidArgument;
    }
    if (loopback->count == 0) {
        return Status::WouldBlock;
    }

    const size_t frame_length = loopback->lengths[loopback->head];
    *out_length = frame_length;
    if (!output || output_capacity < frame_length) {
        return Status::BufferTooSmall;
    }

    copy_bytes(output, loopback->frames[loopback->head], frame_length);
    loopback->lengths[loopback->head] = 0;
    loopback->head = (loopback->head + 1) % LOOPBACK_QUEUE_DEPTH;
    --loopback->count;
    increment(loopback->received_frames);
    return Status::Ok;
}

uint64_t next_neighbor_sequence(NetworkStack& stack) {
    if (stack.neighbor_update_sequence == UINT64_MAX) {
        stack.neighbor_update_sequence = 1;
    } else {
        ++stack.neighbor_update_sequence;
        if (stack.neighbor_update_sequence == 0) {
            stack.neighbor_update_sequence = 1;
        }
    }
    return stack.neighbor_update_sequence;
}

Status update_neighbor(
    NetworkStack& stack,
    const IPv4Address& ip,
    const MacAddress& mac
) {
    if (!ipv4_is_valid_unicast(ip) || !mac_is_valid_unicast(mac)) {
        return Status::MalformedPacket;
    }
    if (stack.ipv4_configured &&
        ipv4_equal(ip, stack.config.address) &&
        !mac_equal(mac, stack.interface->hardware_address)) {
        return Status::MalformedPacket;
    }

    for (size_t i = 0; i < NEIGHBOR_TABLE_CAPACITY; ++i) {
        NeighborEntry& entry = stack.neighbors[i];
        if (entry.state == NeighborState::Reachable && ipv4_equal(entry.ip, ip)) {
            entry.mac = mac;
            entry.update_sequence = next_neighbor_sequence(stack);
            increment(stack.stats.neighbor_updates);
            return Status::Ok;
        }
    }

    size_t selected = NEIGHBOR_TABLE_CAPACITY;
    for (size_t i = 0; i < NEIGHBOR_TABLE_CAPACITY; ++i) {
        if (stack.neighbors[i].state == NeighborState::Empty) {
            selected = i;
            break;
        }
    }
    if (selected == NEIGHBOR_TABLE_CAPACITY) {
        selected = stack.neighbor_eviction_cursor;
        ++stack.neighbor_eviction_cursor;
        if (stack.neighbor_eviction_cursor >= NEIGHBOR_TABLE_CAPACITY) {
            stack.neighbor_eviction_cursor = 1;
        }
        increment(stack.stats.neighbor_evictions);
    }

    NeighborEntry& entry = stack.neighbors[selected];
    entry.state = NeighborState::Reachable;
    entry.ip = ip;
    entry.mac = mac;
    entry.update_sequence = next_neighbor_sequence(stack);
    increment(stack.stats.neighbor_updates);
    return Status::Ok;
}

const NeighborEntry* find_neighbor(
    const NetworkStack& stack,
    const IPv4Address& ip
) {
    for (size_t i = 0; i < NEIGHBOR_TABLE_CAPACITY; ++i) {
        const NeighborEntry& entry = stack.neighbors[i];
        if (entry.state == NeighborState::Reachable && ipv4_equal(entry.ip, ip)) {
            return &entry;
        }
    }
    return nullptr;
}

void record_receive_failure(NetworkStack& stack, Status status) {
    if (status == Status::Ok) {
        return;
    }

    increment(stack.stats.dropped_frames);
    switch (status) {
        case Status::ChecksumMismatch:
            increment(stack.stats.checksum_errors);
            increment(stack.stats.parse_errors);
            break;
        case Status::FrameTooShort:
        case Status::FrameTooLarge:
        case Status::MalformedPacket:
            increment(stack.stats.parse_errors);
            break;
        case Status::UnsupportedProtocol:
        case Status::UnsupportedFragment:
            increment(stack.stats.unsupported_packets);
            break;
        case Status::InterfaceError:
        case Status::QueueFull:
        case Status::BufferTooSmall:
            increment(stack.stats.interface_errors);
            break;
        default:
            break;
    }
}

Status transmit_stack_frame(NetworkStack& stack, size_t frame_length) {
    const Status status =
        interface_transmit(stack.interface, stack.transmit_buffer, frame_length);
    if (status != Status::Ok) {
        increment(stack.stats.interface_errors);
        return status;
    }
    increment(stack.stats.frames_transmitted);
    saturating_add(stack.stats.bytes_transmitted, frame_length);
    return Status::Ok;
}

Status send_arp_packet(
    NetworkStack& stack,
    const MacAddress& ethernet_destination,
    const ArpPacket& arp
) {
    size_t arp_length = 0;
    Status status = serialize_arp(
        arp,
        stack.transmit_buffer + ETHERNET_HEADER_SIZE,
        ETHERNET_MTU,
        &arp_length
    );
    if (status != Status::Ok) {
        return status;
    }

    size_t frame_length = 0;
    status = serialize_ethernet(
        ethernet_destination,
        stack.interface->hardware_address,
        ETHER_TYPE_ARP,
        stack.transmit_buffer + ETHERNET_HEADER_SIZE,
        arp_length,
        stack.transmit_buffer,
        sizeof(stack.transmit_buffer),
        &frame_length
    );
    if (status != Status::Ok) {
        return status;
    }

    status = transmit_stack_frame(stack, frame_length);
    if (status == Status::Ok) {
        increment(stack.stats.arp_transmitted);
    }
    return status;
}

Status send_arp_request(NetworkStack& stack, const IPv4Address& target) {
    const ArpPacket request = {
        ArpOperation::Request,
        stack.interface->hardware_address,
        stack.config.address,
        ZERO_MAC,
        target
    };
    const Status status = send_arp_packet(stack, BROADCAST_MAC, request);
    if (status == Status::Ok) {
        increment(stack.stats.arp_resolution_requests);
    }
    return status;
}

Status send_arp_reply(NetworkStack& stack, const ArpPacket& request) {
    const ArpPacket reply = {
        ArpOperation::Reply,
        stack.interface->hardware_address,
        stack.config.address,
        request.sender_mac,
        request.sender_ip
    };
    return send_arp_packet(stack, request.sender_mac, reply);
}

Status send_icmp(
    NetworkStack& stack,
    const MacAddress& destination_mac,
    const IPv4Address& destination_ip,
    IcmpEchoType type,
    uint16_t identifier,
    uint16_t sequence,
    const uint8_t* payload,
    size_t payload_length
) {
    if (payload_length > ICMP_ECHO_MAX_PAYLOAD) {
        return Status::PayloadTooLarge;
    }

    const size_t icmp_offset = ETHERNET_HEADER_SIZE + IPV4_MIN_HEADER_SIZE;
    size_t icmp_length = 0;
    Status status = serialize_icmp_echo(
        type,
        identifier,
        sequence,
        payload,
        payload_length,
        stack.transmit_buffer + icmp_offset,
        sizeof(stack.transmit_buffer) - icmp_offset,
        &icmp_length
    );
    if (status != Status::Ok) {
        return status;
    }

    IPv4Header header = {
        stack.config.address,
        destination_ip,
        IPV4_PROTOCOL_ICMP,
        64,
        stack.next_identification,
        true
    };
    ++stack.next_identification;

    size_t ipv4_length = 0;
    status = serialize_ipv4(
        header,
        stack.transmit_buffer + icmp_offset,
        icmp_length,
        stack.transmit_buffer + ETHERNET_HEADER_SIZE,
        ETHERNET_MTU,
        &ipv4_length
    );
    if (status != Status::Ok) {
        return status;
    }

    size_t frame_length = 0;
    status = serialize_ethernet(
        destination_mac,
        stack.interface->hardware_address,
        ETHER_TYPE_IPV4,
        stack.transmit_buffer + ETHERNET_HEADER_SIZE,
        ipv4_length,
        stack.transmit_buffer,
        sizeof(stack.transmit_buffer),
        &frame_length
    );
    if (status != Status::Ok) {
        return status;
    }

    status = transmit_stack_frame(stack, frame_length);
    if (status == Status::Ok) {
        increment(stack.stats.ipv4_transmitted);
        increment(stack.stats.icmp_transmitted);
        if (type == IcmpEchoType::Request) {
            increment(stack.stats.echo_requests_sent);
        } else {
            increment(stack.stats.echo_replies_sent);
        }
    }
    return status;
}

Status resolve_destination(
    NetworkStack& stack,
    const IPv4Address& destination,
    const MacAddress** out_mac) {
    IPv4Address next_hop = destination;
    if (!ipv4_on_same_network(
            stack.config.address, destination, stack.config.netmask)) {
        if (ipv4_is_zero(stack.config.gateway)) return Status::NoRoute;
        next_hop = stack.config.gateway;
    }
    const NeighborEntry* neighbor = find_neighbor(stack, next_hop);
    if (neighbor == nullptr) {
        const Status arp_status = send_arp_request(stack, next_hop);
        return arp_status == Status::Ok
            ? Status::NeighborResolutionPending
            : arp_status;
    }
    *out_mac = &neighbor->mac;
    return Status::Ok;
}

Status send_transport_packet(
    NetworkStack& stack,
    const MacAddress& destination_mac,
    const IPv4Address& destination_ip,
    uint8_t protocol,
    const uint8_t* transport,
    size_t transport_length) {
    const size_t transport_offset = ETHERNET_HEADER_SIZE + IPV4_MIN_HEADER_SIZE;
    if (transport != stack.transmit_buffer + transport_offset) {
        return Status::InvalidArgument;
    }
    const IPv4Header header = {
        stack.config.address,
        destination_ip,
        protocol,
        64U,
        stack.next_identification,
        true
    };
    ++stack.next_identification;
    size_t ipv4_length = 0U;
    Status status = serialize_ipv4(
        header,
        transport,
        transport_length,
        stack.transmit_buffer + ETHERNET_HEADER_SIZE,
        ETHERNET_MTU,
        &ipv4_length);
    if (status != Status::Ok) return status;
    size_t frame_length = 0U;
    status = serialize_ethernet(
        destination_mac,
        stack.interface->hardware_address,
        ETHER_TYPE_IPV4,
        stack.transmit_buffer + ETHERNET_HEADER_SIZE,
        ipv4_length,
        stack.transmit_buffer,
        sizeof(stack.transmit_buffer),
        &frame_length);
    if (status != Status::Ok) return status;
    status = transmit_stack_frame(stack, frame_length);
    if (status == Status::Ok) increment(stack.stats.ipv4_transmitted);
    return status;
}

Status handle_arp(
    NetworkStack& stack,
    const EthernetFrameView& ethernet
) {
    ArpPacket arp = {};
    Status status = parse_arp(ethernet.payload, ethernet.payload_length, &arp);
    if (status != Status::Ok) {
        return status;
    }
    increment(stack.stats.arp_received);

    if (!mac_equal(arp.sender_mac, ethernet.source) ||
        !mac_is_valid_unicast(arp.sender_mac) ||
        !ipv4_is_valid_unicast(arp.sender_ip)) {
        return Status::MalformedPacket;
    }

    if (arp.operation == ArpOperation::Reply) {
        if (!ipv4_equal(arp.target_ip, stack.config.address) ||
            !mac_equal(arp.target_mac, stack.interface->hardware_address)) {
            return Status::NotForUs;
        }
        return update_neighbor(stack, arp.sender_ip, arp.sender_mac);
    }

    status = update_neighbor(stack, arp.sender_ip, arp.sender_mac);
    if (status != Status::Ok) {
        return status;
    }
    if (!ipv4_equal(arp.target_ip, stack.config.address)) {
        return Status::NotForUs;
    }
    return send_arp_reply(stack, arp);
}

Status handle_ipv4(
    NetworkStack& stack,
    const EthernetFrameView& ethernet
) {
    IPv4PacketView ipv4 = {};
    Status status = parse_ipv4(ethernet.payload, ethernet.payload_length, &ipv4);
    if (status != Status::Ok) {
        return status;
    }
    increment(stack.stats.ipv4_received);

    if (!ipv4_equal(ipv4.destination, stack.config.address)) {
        return Status::NotForUs;
    }
    if (!ipv4_is_valid_unicast(ipv4.source)) {
        return Status::MalformedPacket;
    }
    status = update_neighbor(stack, ipv4.source, ethernet.source);
    if (status != Status::Ok) return status;

    if (ipv4.protocol == IPV4_PROTOCOL_UDP) {
        UdpDatagramView udp{};
        status = parse_udp(
            ipv4.source, ipv4.destination,
            ipv4.payload, ipv4.payload_length, &udp);
        if (status != Status::Ok) return status;
        if (udp.payload_length > TRANSPORT_INBOX_CAPACITY) {
            return Status::BufferTooSmall;
        }
        stack.last_udp_datagram = {};
        stack.last_udp_datagram.valid = true;
        stack.last_udp_datagram.source = ipv4.source;
        stack.last_udp_datagram.destination = ipv4.destination;
        stack.last_udp_datagram.source_port = udp.source_port;
        stack.last_udp_datagram.destination_port = udp.destination_port;
        stack.last_udp_datagram.payload_length = udp.payload_length;
        copy_bytes(stack.last_udp_datagram.payload, udp.payload,
                   udp.payload_length);
        increment(stack.stats.udp_received);
        return Status::Ok;
    }
    if (ipv4.protocol == IPV4_PROTOCOL_TCP) {
        TcpSegmentView tcp{};
        status = parse_tcp(
            ipv4.source, ipv4.destination,
            ipv4.payload, ipv4.payload_length, &tcp);
        if (status != Status::Ok) return status;
        if (tcp.payload_length > TRANSPORT_INBOX_CAPACITY) {
            return Status::BufferTooSmall;
        }
        stack.last_tcp_segment = {};
        stack.last_tcp_segment.valid = true;
        stack.last_tcp_segment.source = ipv4.source;
        stack.last_tcp_segment.destination = ipv4.destination;
        stack.last_tcp_segment.source_port = tcp.source_port;
        stack.last_tcp_segment.destination_port = tcp.destination_port;
        stack.last_tcp_segment.sequence = tcp.sequence;
        stack.last_tcp_segment.acknowledgement = tcp.acknowledgement;
        stack.last_tcp_segment.flags = tcp.flags;
        stack.last_tcp_segment.window = tcp.window;
        stack.last_tcp_segment.payload_length = tcp.payload_length;
        copy_bytes(stack.last_tcp_segment.payload, tcp.payload,
                   tcp.payload_length);
        increment(stack.stats.tcp_received);
        return Status::Ok;
    }
    if (ipv4.protocol != IPV4_PROTOCOL_ICMP) return Status::UnsupportedProtocol;

    IcmpEchoView echo = {};
    status = parse_icmp_echo(ipv4.payload, ipv4.payload_length, &echo);
    if (status != Status::Ok) {
        return status;
    }
    increment(stack.stats.icmp_received);

    if (echo.type == IcmpEchoType::Request) {
        increment(stack.stats.echo_requests_received);
        return send_icmp(
            stack,
            ethernet.source,
            ipv4.source,
            IcmpEchoType::Reply,
            echo.identifier,
            echo.sequence,
            echo.payload,
            echo.payload_length
        );
    }

    increment(stack.stats.echo_replies_received);
    stack.last_ping_reply.valid = true;
    stack.last_ping_reply.source = ipv4.source;
    stack.last_ping_reply.identifier = echo.identifier;
    stack.last_ping_reply.sequence = echo.sequence;
    stack.last_ping_reply.payload_length = echo.payload_length;
    return Status::Ok;
}

} // namespace

bool mac_equal(const MacAddress& left, const MacAddress& right) {
    for (size_t i = 0; i < MAC_ADDRESS_LENGTH; ++i) {
        if (left.bytes[i] != right.bytes[i]) {
            return false;
        }
    }
    return true;
}

bool mac_is_zero(const MacAddress& address) {
    for (size_t i = 0; i < MAC_ADDRESS_LENGTH; ++i) {
        if (address.bytes[i] != 0) {
            return false;
        }
    }
    return true;
}

bool mac_is_broadcast(const MacAddress& address) {
    return mac_equal(address, BROADCAST_MAC);
}

bool mac_is_multicast(const MacAddress& address) {
    return (address.bytes[0] & 1u) != 0;
}

bool ipv4_equal(const IPv4Address& left, const IPv4Address& right) {
    for (size_t i = 0; i < IPV4_ADDRESS_LENGTH; ++i) {
        if (left.bytes[i] != right.bytes[i]) {
            return false;
        }
    }
    return true;
}

bool ipv4_is_zero(const IPv4Address& address) {
    return ipv4_value(address) == 0;
}

bool ipv4_is_multicast(const IPv4Address& address) {
    return address.bytes[0] >= 224 && address.bytes[0] <= 239;
}

bool ipv4_is_limited_broadcast(const IPv4Address& address) {
    return ipv4_value(address) == UINT32_MAX;
}

uint16_t read_be16(const uint8_t* data) {
    if (!data) {
        return 0;
    }
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(data[0]) << 8) |
        static_cast<uint16_t>(data[1])
    );
}

uint32_t read_be32(const uint8_t* data) {
    if (!data) {
        return 0;
    }
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

void write_be16(uint8_t* data, uint16_t value) {
    if (!data) {
        return;
    }
    data[0] = static_cast<uint8_t>(value >> 8);
    data[1] = static_cast<uint8_t>(value);
}

void write_be32(uint8_t* data, uint32_t value) {
    if (!data) {
        return;
    }
    data[0] = static_cast<uint8_t>(value >> 24);
    data[1] = static_cast<uint8_t>(value >> 16);
    data[2] = static_cast<uint8_t>(value >> 8);
    data[3] = static_cast<uint8_t>(value);
}

uint16_t internet_checksum(const uint8_t* data, size_t length) {
    if (!data && length != 0) {
        return UINT16_MAX;
    }

    uint32_t sum = 0;
    size_t position = 0;
    while (position + 1 < length) {
        sum += read_be16(data + position);
        sum = (sum & UINT32_C(0xffff)) + (sum >> 16);
        position += 2;
    }
    if (position < length) {
        sum += static_cast<uint16_t>(data[position]) << 8;
    }
    while ((sum >> 16) != 0) {
        sum = (sum & UINT32_C(0xffff)) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum);
}

Status parse_ethernet(
    const uint8_t* frame,
    size_t frame_length,
    EthernetFrameView* out_frame
) {
    if (!frame || !out_frame) {
        return Status::InvalidArgument;
    }
    if (frame_length < ETHERNET_HEADER_SIZE) {
        return Status::FrameTooShort;
    }
    if (frame_length > ETHERNET_MAX_FRAME_SIZE) {
        return Status::FrameTooLarge;
    }

    clear_bytes(out_frame, sizeof(*out_frame));
    copy_bytes(out_frame->destination.bytes, frame, MAC_ADDRESS_LENGTH);
    copy_bytes(
        out_frame->source.bytes,
        frame + MAC_ADDRESS_LENGTH,
        MAC_ADDRESS_LENGTH
    );
    out_frame->ether_type = read_be16(frame + 12);
    if (out_frame->ether_type < UINT16_C(0x0600)) {
        return Status::UnsupportedProtocol;
    }
    out_frame->payload = frame + ETHERNET_HEADER_SIZE;
    out_frame->payload_length = frame_length - ETHERNET_HEADER_SIZE;
    return Status::Ok;
}

Status serialize_ethernet(
    const MacAddress& destination,
    const MacAddress& source,
    uint16_t ether_type,
    const uint8_t* payload,
    size_t payload_length,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length
) {
    if (out_length) {
        *out_length = 0;
    }
    if (!output || !out_length || (!payload && payload_length != 0)) {
        return Status::InvalidArgument;
    }
    if (ether_type < UINT16_C(0x0600)) {
        return Status::UnsupportedProtocol;
    }
    if (payload_length > ETHERNET_MTU) {
        return Status::PayloadTooLarge;
    }
    const size_t required = ETHERNET_HEADER_SIZE + payload_length;
    if (output_capacity < required) {
        *out_length = required;
        return Status::BufferTooSmall;
    }
    if (ranges_overlap(output, required, payload, payload_length) &&
        payload != output + ETHERNET_HEADER_SIZE) {
        return Status::InvalidArgument;
    }

    copy_bytes(output, destination.bytes, MAC_ADDRESS_LENGTH);
    copy_bytes(output + MAC_ADDRESS_LENGTH, source.bytes, MAC_ADDRESS_LENGTH);
    write_be16(output + 12, ether_type);
    if (payload_length != 0) {
        copy_bytes(output + ETHERNET_HEADER_SIZE, payload, payload_length);
    }
    *out_length = required;
    return Status::Ok;
}

Status parse_arp(
    const uint8_t* packet,
    size_t packet_length,
    ArpPacket* out_packet
) {
    if (!packet || !out_packet) {
        return Status::InvalidArgument;
    }
    if (packet_length < ARP_PACKET_SIZE) {
        return Status::FrameTooShort;
    }
    if (read_be16(packet) != 1 ||
        read_be16(packet + 2) != ETHER_TYPE_IPV4 ||
        packet[4] != MAC_ADDRESS_LENGTH ||
        packet[5] != IPV4_ADDRESS_LENGTH) {
        return Status::MalformedPacket;
    }

    const uint16_t operation = read_be16(packet + 6);
    if (operation != static_cast<uint16_t>(ArpOperation::Request) &&
        operation != static_cast<uint16_t>(ArpOperation::Reply)) {
        return Status::MalformedPacket;
    }

    clear_bytes(out_packet, sizeof(*out_packet));
    out_packet->operation = static_cast<ArpOperation>(operation);
    copy_bytes(out_packet->sender_mac.bytes, packet + 8, MAC_ADDRESS_LENGTH);
    copy_bytes(out_packet->sender_ip.bytes, packet + 14, IPV4_ADDRESS_LENGTH);
    copy_bytes(out_packet->target_mac.bytes, packet + 18, MAC_ADDRESS_LENGTH);
    copy_bytes(out_packet->target_ip.bytes, packet + 24, IPV4_ADDRESS_LENGTH);
    return Status::Ok;
}

Status serialize_arp(
    const ArpPacket& packet,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length
) {
    if (out_length) {
        *out_length = 0;
    }
    if (!output || !out_length) {
        return Status::InvalidArgument;
    }
    if (packet.operation != ArpOperation::Request &&
        packet.operation != ArpOperation::Reply) {
        return Status::MalformedPacket;
    }
    if (output_capacity < ARP_PACKET_SIZE) {
        *out_length = ARP_PACKET_SIZE;
        return Status::BufferTooSmall;
    }

    write_be16(output, 1);
    write_be16(output + 2, ETHER_TYPE_IPV4);
    output[4] = MAC_ADDRESS_LENGTH;
    output[5] = IPV4_ADDRESS_LENGTH;
    write_be16(output + 6, static_cast<uint16_t>(packet.operation));
    copy_bytes(output + 8, packet.sender_mac.bytes, MAC_ADDRESS_LENGTH);
    copy_bytes(output + 14, packet.sender_ip.bytes, IPV4_ADDRESS_LENGTH);
    copy_bytes(output + 18, packet.target_mac.bytes, MAC_ADDRESS_LENGTH);
    copy_bytes(output + 24, packet.target_ip.bytes, IPV4_ADDRESS_LENGTH);
    *out_length = ARP_PACKET_SIZE;
    return Status::Ok;
}

Status parse_ipv4(
    const uint8_t* packet,
    size_t packet_length,
    IPv4PacketView* out_packet
) {
    if (!packet || !out_packet) {
        return Status::InvalidArgument;
    }
    if (packet_length < IPV4_MIN_HEADER_SIZE) {
        return Status::FrameTooShort;
    }

    const uint8_t version = packet[0] >> 4;
    const uint8_t ihl_words = packet[0] & 0x0fu;
    if (version != 4 || ihl_words < 5) {
        return Status::MalformedPacket;
    }
    const size_t header_length = static_cast<size_t>(ihl_words) * 4;
    if (header_length > IPV4_MAX_HEADER_SIZE || header_length > packet_length) {
        return Status::MalformedPacket;
    }

    const size_t total_length = read_be16(packet + 2);
    if (total_length < header_length || total_length > packet_length) {
        return Status::MalformedPacket;
    }
    if (internet_checksum(packet, header_length) != 0) {
        return Status::ChecksumMismatch;
    }

    const uint16_t flags_and_fragment = read_be16(packet + 6);
    if ((flags_and_fragment & UINT16_C(0x8000)) != 0) {
        return Status::MalformedPacket;
    }
    if ((flags_and_fragment & UINT16_C(0x2000)) != 0 ||
        (flags_and_fragment & UINT16_C(0x1fff)) != 0) {
        return Status::UnsupportedFragment;
    }
    if (packet[8] == 0) {
        return Status::MalformedPacket;
    }

    clear_bytes(out_packet, sizeof(*out_packet));
    copy_bytes(out_packet->source.bytes, packet + 12, IPV4_ADDRESS_LENGTH);
    copy_bytes(out_packet->destination.bytes, packet + 16, IPV4_ADDRESS_LENGTH);
    out_packet->protocol = packet[9];
    out_packet->ttl = packet[8];
    out_packet->identification = read_be16(packet + 4);
    out_packet->dont_fragment =
        (flags_and_fragment & UINT16_C(0x4000)) != 0;
    out_packet->payload = packet + header_length;
    out_packet->payload_length = total_length - header_length;
    out_packet->header_length = header_length;
    return Status::Ok;
}

Status serialize_ipv4(
    const IPv4Header& header,
    const uint8_t* payload,
    size_t payload_length,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length
) {
    if (out_length) {
        *out_length = 0;
    }
    if (!output || !out_length || (!payload && payload_length != 0)) {
        return Status::InvalidArgument;
    }
    if (header.ttl == 0) {
        return Status::MalformedPacket;
    }
    if (payload_length > UINT16_MAX - IPV4_MIN_HEADER_SIZE) {
        return Status::PayloadTooLarge;
    }

    const size_t required = IPV4_MIN_HEADER_SIZE + payload_length;
    if (output_capacity < required) {
        *out_length = required;
        return Status::BufferTooSmall;
    }
    if (ranges_overlap(output, required, payload, payload_length) &&
        payload != output + IPV4_MIN_HEADER_SIZE) {
        return Status::InvalidArgument;
    }

    clear_bytes(output, IPV4_MIN_HEADER_SIZE);
    output[0] = 0x45;
    write_be16(output + 2, static_cast<uint16_t>(required));
    write_be16(output + 4, header.identification);
    write_be16(output + 6, header.dont_fragment ? UINT16_C(0x4000) : 0);
    output[8] = header.ttl;
    output[9] = header.protocol;
    copy_bytes(output + 12, header.source.bytes, IPV4_ADDRESS_LENGTH);
    copy_bytes(output + 16, header.destination.bytes, IPV4_ADDRESS_LENGTH);
    write_be16(output + 10, internet_checksum(output, IPV4_MIN_HEADER_SIZE));
    if (payload_length != 0) {
        copy_bytes(output + IPV4_MIN_HEADER_SIZE, payload, payload_length);
    }
    *out_length = required;
    return Status::Ok;
}

Status parse_icmp_echo(
    const uint8_t* packet,
    size_t packet_length,
    IcmpEchoView* out_echo
) {
    if (!packet || !out_echo) {
        return Status::InvalidArgument;
    }
    if (packet_length < ICMP_ECHO_HEADER_SIZE) {
        return Status::FrameTooShort;
    }
    if (internet_checksum(packet, packet_length) != 0) {
        return Status::ChecksumMismatch;
    }
    if ((packet[0] != static_cast<uint8_t>(IcmpEchoType::Request) &&
         packet[0] != static_cast<uint8_t>(IcmpEchoType::Reply)) ||
        packet[1] != 0) {
        return Status::UnsupportedProtocol;
    }

    clear_bytes(out_echo, sizeof(*out_echo));
    out_echo->type = static_cast<IcmpEchoType>(packet[0]);
    out_echo->identifier = read_be16(packet + 4);
    out_echo->sequence = read_be16(packet + 6);
    out_echo->payload = packet + ICMP_ECHO_HEADER_SIZE;
    out_echo->payload_length = packet_length - ICMP_ECHO_HEADER_SIZE;
    return Status::Ok;
}

Status serialize_icmp_echo(
    IcmpEchoType type,
    uint16_t identifier,
    uint16_t sequence,
    const uint8_t* payload,
    size_t payload_length,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length
) {
    if (out_length) {
        *out_length = 0;
    }
    if (!output || !out_length || (!payload && payload_length != 0)) {
        return Status::InvalidArgument;
    }
    if (type != IcmpEchoType::Request && type != IcmpEchoType::Reply) {
        return Status::UnsupportedProtocol;
    }
    if (payload_length > ICMP_ECHO_MAX_PAYLOAD) {
        return Status::PayloadTooLarge;
    }

    const size_t required = ICMP_ECHO_HEADER_SIZE + payload_length;
    if (output_capacity < required) {
        *out_length = required;
        return Status::BufferTooSmall;
    }
    if (ranges_overlap(output, required, payload, payload_length) &&
        payload != output + ICMP_ECHO_HEADER_SIZE) {
        return Status::InvalidArgument;
    }

    clear_bytes(output, ICMP_ECHO_HEADER_SIZE);
    output[0] = static_cast<uint8_t>(type);
    write_be16(output + 4, identifier);
    write_be16(output + 6, sequence);
    if (payload_length != 0) {
        copy_bytes(output + ICMP_ECHO_HEADER_SIZE, payload, payload_length);
    }
    write_be16(output + 2, internet_checksum(output, required));
    *out_length = required;
    return Status::Ok;
}

Status interface_transmit(
    NetworkInterface* interface,
    const uint8_t* frame,
    size_t frame_length
) {
    if (!interface || !interface->transmit || !frame || frame_length == 0) {
        return Status::InvalidArgument;
    }
    if (interface->mtu == 0 ||
        interface->mtu > ETHERNET_MTU ||
        frame_length > interface->mtu + ETHERNET_HEADER_SIZE) {
        return Status::FrameTooLarge;
    }
    const Status status =
        interface->transmit(interface->context, frame, frame_length);
    return status == Status::Ok ||
                   status == Status::QueueFull ||
                   status == Status::NotInitialized
               ? status
               : Status::InterfaceError;
}

Status interface_receive(
    NetworkInterface* interface,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length
) {
    if (out_length) {
        *out_length = 0;
    }
    if (!interface || !interface->receive || !output || !out_length) {
        return Status::InvalidArgument;
    }
    const Status status = interface->receive(
        interface->context,
        output,
        output_capacity,
        out_length
    );
    if (status == Status::Ok && *out_length > output_capacity) {
        *out_length = 0;
        return Status::InterfaceError;
    }
    return status == Status::Ok ||
                   status == Status::WouldBlock ||
                   status == Status::BufferTooSmall ||
                   status == Status::NotInitialized
               ? status
               : Status::InterfaceError;
}

Status initialize_loopback(
    LoopbackInterface* loopback,
    const MacAddress& hardware_address
) {
    if (!loopback || !mac_is_valid_unicast(hardware_address)) {
        return Status::InvalidArgument;
    }

    clear_bytes(loopback, sizeof(*loopback));
    loopback->interface.context = loopback;
    loopback->interface.transmit = loopback_transmit;
    loopback->interface.receive = loopback_receive;
    loopback->interface.hardware_address = hardware_address;
    loopback->interface.mtu = ETHERNET_MTU;
    loopback->initialized = true;
    return Status::Ok;
}

size_t loopback_queued_frames(const LoopbackInterface* loopback) {
    return loopback && loopback->initialized ? loopback->count : 0;
}

Status initialize_stack(NetworkStack* stack, NetworkInterface* interface) {
    if (!stack || !interface || !interface->transmit || !interface->receive) {
        return Status::InvalidArgument;
    }
    if (!mac_is_valid_unicast(interface->hardware_address) ||
        interface->mtu < 68 ||
        interface->mtu > ETHERNET_MTU) {
        return Status::InvalidConfiguration;
    }

    clear_bytes(stack, sizeof(*stack));
    stack->interface = interface;
    stack->next_identification = 1;
    stack->initialized = true;
    return Status::Ok;
}

Status configure_ipv4(NetworkStack* stack, const IPv4Config& config) {
    if (!stack || !stack->initialized) {
        return Status::NotInitialized;
    }
    if (!netmask_is_contiguous(config.netmask) ||
        !config_address_is_host(config.address, config.netmask)) {
        return Status::InvalidConfiguration;
    }
    if (!ipv4_is_zero(config.gateway) &&
        (!config_address_is_host(config.gateway, config.netmask) ||
         !ipv4_on_same_network(config.address, config.gateway, config.netmask))) {
        return Status::InvalidConfiguration;
    }

    stack->config = config;
    clear_bytes(stack->neighbors, sizeof(stack->neighbors));
    clear_bytes(&stack->last_ping_reply, sizeof(stack->last_ping_reply));
    clear_bytes(&stack->last_udp_datagram, sizeof(stack->last_udp_datagram));
    clear_bytes(&stack->last_tcp_segment, sizeof(stack->last_tcp_segment));
    stack->neighbor_eviction_cursor = 1;
    stack->neighbor_update_sequence = 1;

    NeighborEntry& self = stack->neighbors[0];
    self.state = NeighborState::Reachable;
    self.ip = config.address;
    self.mac = stack->interface->hardware_address;
    self.update_sequence = 1;
    stack->ipv4_configured = true;
    return Status::Ok;
}

Status receive(NetworkStack* stack, const uint8_t* frame, size_t frame_length) {
    if (!stack || !stack->initialized) {
        return Status::NotInitialized;
    }
    if (!stack->ipv4_configured) {
        return Status::NotConfigured;
    }
    if (!frame || frame_length == 0) {
        return Status::InvalidArgument;
    }

    increment(stack->stats.frames_received);
    saturating_add(stack->stats.bytes_received, frame_length);

    EthernetFrameView ethernet = {};
    Status status = parse_ethernet(frame, frame_length, &ethernet);
    if (status != Status::Ok) {
        record_receive_failure(*stack, status);
        return status;
    }

    if (!mac_equal(ethernet.destination, stack->interface->hardware_address) &&
        !mac_is_broadcast(ethernet.destination)) {
        status = Status::NotForUs;
    } else if (!mac_is_valid_unicast(ethernet.source)) {
        status = Status::MalformedPacket;
    } else if (ethernet.ether_type == ETHER_TYPE_ARP) {
        status = handle_arp(*stack, ethernet);
    } else if (ethernet.ether_type == ETHER_TYPE_IPV4) {
        status = handle_ipv4(*stack, ethernet);
    } else {
        status = Status::UnsupportedProtocol;
    }

    if (status != Status::Ok) {
        record_receive_failure(*stack, status);
    }
    return status;
}

Status poll(NetworkStack* stack, size_t budget, size_t* out_processed) {
    if (out_processed) {
        *out_processed = 0;
    }
    if (!stack || !stack->initialized) {
        return Status::NotInitialized;
    }
    if (!stack->ipv4_configured) {
        return Status::NotConfigured;
    }

    size_t processed = 0;
    Status first_error = Status::Ok;
    while (processed < budget) {
        size_t frame_length = 0;
        const Status interface_status = interface_receive(
            stack->interface,
            stack->receive_buffer,
            sizeof(stack->receive_buffer),
            &frame_length
        );
        if (interface_status == Status::WouldBlock) {
            break;
        }
        if (interface_status != Status::Ok) {
            increment(stack->stats.interface_errors);
            first_error = interface_status;
            break;
        }

        ++processed;
        const Status receive_status =
            receive(stack, stack->receive_buffer, frame_length);
        if (receive_status != Status::Ok && first_error == Status::Ok) {
            first_error = receive_status;
        }
    }

    if (out_processed) {
        *out_processed = processed;
    }
    return first_error;
}

Status send_ping(
    NetworkStack* stack,
    const IPv4Address& destination,
    uint16_t identifier,
    uint16_t sequence,
    const uint8_t* payload,
    size_t payload_length
) {
    if (!stack || !stack->initialized) {
        return Status::NotInitialized;
    }
    if (!stack->ipv4_configured) {
        return Status::NotConfigured;
    }
    if ((!payload && payload_length != 0) ||
        !ipv4_is_valid_unicast(destination)) {
        return Status::InvalidArgument;
    }
    if (payload_length > ICMP_ECHO_MAX_PAYLOAD) {
        return Status::PayloadTooLarge;
    }

    IPv4Address next_hop = destination;
    if (!ipv4_on_same_network(
            stack->config.address,
            destination,
            stack->config.netmask
        )) {
        if (ipv4_is_zero(stack->config.gateway)) {
            return Status::NoRoute;
        }
        next_hop = stack->config.gateway;
    }

    const NeighborEntry* neighbor = find_neighbor(*stack, next_hop);
    if (!neighbor) {
        const Status arp_status = send_arp_request(*stack, next_hop);
        return arp_status == Status::Ok
            ? Status::NeighborResolutionPending
            : arp_status;
    }

    return send_icmp(
        *stack,
        neighbor->mac,
        destination,
        IcmpEchoType::Request,
        identifier,
        sequence,
        payload,
        payload_length
    );
}

Status send_udp(
    NetworkStack* stack,
    const IPv4Address& destination,
    uint16_t source_port,
    uint16_t destination_port,
    const uint8_t* payload,
    size_t payload_length) {
    if (stack == nullptr || !stack->initialized) return Status::NotInitialized;
    if (!stack->ipv4_configured) return Status::NotConfigured;
    if ((!payload && payload_length != 0U) || source_port == 0U ||
        destination_port == 0U || !ipv4_is_valid_unicast(destination)) {
        return Status::InvalidArgument;
    }
    const MacAddress* destination_mac = nullptr;
    Status status = resolve_destination(*stack, destination, &destination_mac);
    if (status != Status::Ok) return status;
    const size_t offset = ETHERNET_HEADER_SIZE + IPV4_MIN_HEADER_SIZE;
    size_t udp_length = 0U;
    status = serialize_udp(
        stack->config.address, destination,
        source_port, destination_port,
        payload, payload_length,
        stack->transmit_buffer + offset,
        sizeof(stack->transmit_buffer) - offset,
        &udp_length);
    if (status != Status::Ok) return status;
    status = send_transport_packet(
        *stack, *destination_mac, destination,
        IPV4_PROTOCOL_UDP, stack->transmit_buffer + offset, udp_length);
    if (status == Status::Ok) increment(stack->stats.udp_transmitted);
    return status;
}

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
    size_t payload_length) {
    if (stack == nullptr || !stack->initialized) return Status::NotInitialized;
    if (!stack->ipv4_configured) return Status::NotConfigured;
    if ((!payload && payload_length != 0U) || source_port == 0U ||
        destination_port == 0U || !ipv4_is_valid_unicast(destination)) {
        return Status::InvalidArgument;
    }
    const MacAddress* destination_mac = nullptr;
    Status status = resolve_destination(*stack, destination, &destination_mac);
    if (status != Status::Ok) return status;
    const size_t offset = ETHERNET_HEADER_SIZE + IPV4_MIN_HEADER_SIZE;
    size_t tcp_length = 0U;
    status = serialize_tcp(
        stack->config.address, destination,
        source_port, destination_port,
        sequence, acknowledgement, flags, window,
        payload, payload_length,
        stack->transmit_buffer + offset,
        sizeof(stack->transmit_buffer) - offset,
        &tcp_length);
    if (status != Status::Ok) return status;
    status = send_transport_packet(
        *stack, *destination_mac, destination,
        IPV4_PROTOCOL_TCP, stack->transmit_buffer + offset, tcp_length);
    if (status == Status::Ok) increment(stack->stats.tcp_transmitted);
    return status;
}

Status lookup_neighbor(
    const NetworkStack* stack,
    const IPv4Address& address,
    NeighborEntry* out_entry
) {
    if (!stack || !stack->initialized) {
        return Status::NotInitialized;
    }
    if (!out_entry) {
        return Status::InvalidArgument;
    }
    const NeighborEntry* entry = find_neighbor(*stack, address);
    if (!entry) {
        clear_bytes(out_entry, sizeof(*out_entry));
        return Status::WouldBlock;
    }
    *out_entry = *entry;
    return Status::Ok;
}

Status list_neighbors(
    const NetworkStack* stack,
    NeighborCallback callback,
    void* context
) {
    if (!stack || !stack->initialized) {
        return Status::NotInitialized;
    }
    if (!callback) {
        return Status::InvalidArgument;
    }

    for (size_t i = 0; i < NEIGHBOR_TABLE_CAPACITY; ++i) {
        if (stack->neighbors[i].state == NeighborState::Reachable &&
            !callback(&stack->neighbors[i], context)) {
            return Status::IterationStopped;
        }
    }
    return Status::Ok;
}

Status get_stats(const NetworkStack* stack, NetworkStats* out_stats) {
    if (!stack || !stack->initialized) {
        return Status::NotInitialized;
    }
    if (!out_stats) {
        return Status::InvalidArgument;
    }
    *out_stats = stack->stats;
    return Status::Ok;
}

Status get_last_ping_reply(const NetworkStack* stack, PingReply* out_reply) {
    if (!stack || !stack->initialized) {
        return Status::NotInitialized;
    }
    if (!out_reply) {
        return Status::InvalidArgument;
    }
    *out_reply = stack->last_ping_reply;
    return out_reply->valid ? Status::Ok : Status::WouldBlock;
}

Status take_udp_datagram(NetworkStack* stack, UdpDatagram* out_datagram) {
    if (stack == nullptr || !stack->initialized) return Status::NotInitialized;
    if (out_datagram == nullptr) return Status::InvalidArgument;
    if (!stack->last_udp_datagram.valid) return Status::WouldBlock;
    *out_datagram = stack->last_udp_datagram;
    stack->last_udp_datagram.valid = false;
    return Status::Ok;
}

Status take_tcp_segment(NetworkStack* stack, TcpSegment* out_segment) {
    if (stack == nullptr || !stack->initialized) return Status::NotInitialized;
    if (out_segment == nullptr) return Status::InvalidArgument;
    if (!stack->last_tcp_segment.valid) return Status::WouldBlock;
    *out_segment = stack->last_tcp_segment;
    stack->last_tcp_segment.valid = false;
    return Status::Ok;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::NotInitialized: return "not initialized";
        case Status::NotConfigured: return "not configured";
        case Status::InvalidArgument: return "invalid argument";
        case Status::InvalidConfiguration: return "invalid configuration";
        case Status::WouldBlock: return "would block";
        case Status::QueueFull: return "queue full";
        case Status::BufferTooSmall: return "buffer too small";
        case Status::FrameTooShort: return "frame too short";
        case Status::FrameTooLarge: return "frame too large";
        case Status::PayloadTooLarge: return "payload too large";
        case Status::MalformedPacket: return "malformed packet";
        case Status::ChecksumMismatch: return "checksum mismatch";
        case Status::UnsupportedProtocol: return "unsupported protocol";
        case Status::UnsupportedFragment: return "unsupported fragment";
        case Status::NotForUs: return "not for this host";
        case Status::NoRoute: return "no route";
        case Status::NeighborResolutionPending:
            return "neighbor resolution pending";
        case Status::IterationStopped: return "iteration stopped";
        case Status::InterfaceError: return "interface error";
        case Status::NameNotFound: return "DNS name not found";
    }
    return "unknown status";
}

} // namespace net

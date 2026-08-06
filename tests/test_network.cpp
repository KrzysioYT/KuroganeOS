#include "../kernel/net/network.hpp"

namespace {

bool bytes_equal(const uint8_t* left, const uint8_t* right, size_t length) {
    if (!left || !right) {
        return left == right && length == 0;
    }
    for (size_t i = 0; i < length; ++i) {
        if (left[i] != right[i]) {
            return false;
        }
    }
    return true;
}

void copy_bytes(uint8_t* destination, const uint8_t* source, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        destination[i] = source[i];
    }
}

bool text_equals(const char* left, const char* right) {
    if (!left || !right) {
        return left == right;
    }
    while (*left != '\0' && *right != '\0') {
        if (*left != *right) {
            return false;
        }
        ++left;
        ++right;
    }
    return *left == *right;
}

struct NeighborListState {
    size_t count;
};

bool count_neighbor(const net::NeighborEntry*, void* context) {
    NeighborListState* state = static_cast<NeighborListState*>(context);
    ++state->count;
    return true;
}

bool stop_neighbor_list(const net::NeighborEntry*, void* context) {
    NeighborListState* state = static_cast<NeighborListState*>(context);
    ++state->count;
    return false;
}

net::Status make_arp_frame(
    net::ArpOperation operation,
    const net::MacAddress& ethernet_destination,
    const net::MacAddress& sender_mac,
    const net::IPv4Address& sender_ip,
    const net::MacAddress& target_mac,
    const net::IPv4Address& target_ip,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length
) {
    uint8_t arp_bytes[net::ARP_PACKET_SIZE] = {};
    const net::ArpPacket arp = {
        operation,
        sender_mac,
        sender_ip,
        target_mac,
        target_ip
    };
    size_t arp_length = 0;
    net::Status status = net::serialize_arp(
        arp,
        arp_bytes,
        sizeof(arp_bytes),
        &arp_length
    );
    if (status != net::Status::Ok) {
        return status;
    }
    return net::serialize_ethernet(
        ethernet_destination,
        sender_mac,
        net::ETHER_TYPE_ARP,
        arp_bytes,
        arp_length,
        output,
        output_capacity,
        out_length
    );
}

} // namespace

int main() {
    static uint8_t frame[net::ETHERNET_MAX_FRAME_SIZE] = {};
    static uint8_t second_frame[net::ETHERNET_MAX_FRAME_SIZE] = {};
    static uint8_t packet[net::ETHERNET_MTU] = {};
    static uint8_t second_packet[net::ETHERNET_MTU] = {};
    static net::LoopbackInterface loopback = {};
    static net::NetworkStack stack = {};

    const net::MacAddress local_mac = {{0x02, 0x00, 0x00, 0x00, 0x00, 0x02}};
    const net::MacAddress remote_mac = {{0x02, 0x00, 0x00, 0x00, 0x00, 0x03}};
    const net::MacAddress broadcast_mac =
        {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
    const net::IPv4Address local_ip = {{10, 0, 0, 2}};
    const net::IPv4Address remote_ip = {{10, 0, 0, 3}};
    const net::IPv4Address netmask = {{255, 255, 255, 0}};
    const net::IPv4Address gateway = {{10, 0, 0, 1}};
    const net::IPv4Address zero_ip = {{0, 0, 0, 0}};

    net::write_be16(packet, UINT16_C(0xabcd));
    net::write_be32(packet + 2, UINT32_C(0x12345678));
    if (net::read_be16(packet) != UINT16_C(0xabcd) ||
        net::read_be32(packet + 2) != UINT32_C(0x12345678)) {
        return 1;
    }

    const uint8_t ethernet_payload[] = {0x10, 0x20, 0x30};
    size_t frame_length = 0;
    if (net::serialize_ethernet(
            local_mac,
            remote_mac,
            net::ETHER_TYPE_IPV4,
            ethernet_payload,
            sizeof(ethernet_payload),
            frame,
            sizeof(frame),
            &frame_length
        ) != net::Status::Ok ||
        frame_length != net::ETHERNET_HEADER_SIZE + sizeof(ethernet_payload)) {
        return 2;
    }

    net::EthernetFrameView ethernet = {};
    if (net::parse_ethernet(frame, frame_length, &ethernet) != net::Status::Ok ||
        !net::mac_equal(ethernet.destination, local_mac) ||
        !net::mac_equal(ethernet.source, remote_mac) ||
        ethernet.ether_type != net::ETHER_TYPE_IPV4 ||
        ethernet.payload_length != sizeof(ethernet_payload) ||
        !bytes_equal(ethernet.payload, ethernet_payload, sizeof(ethernet_payload))) {
        return 3;
    }
    size_t required = 0;
    if (net::parse_ethernet(frame, net::ETHERNET_HEADER_SIZE - 1, &ethernet) !=
            net::Status::FrameTooShort ||
        net::serialize_ethernet(
            local_mac,
            remote_mac,
            net::ETHER_TYPE_IPV4,
            ethernet_payload,
            sizeof(ethernet_payload),
            second_frame,
            net::ETHERNET_HEADER_SIZE,
            &required
        ) != net::Status::BufferTooSmall ||
        required != net::ETHERNET_HEADER_SIZE + sizeof(ethernet_payload) ||
        net::serialize_ethernet(
            local_mac,
            remote_mac,
            100,
            ethernet_payload,
            sizeof(ethernet_payload),
            second_frame,
            sizeof(second_frame),
            &required
        ) != net::Status::UnsupportedProtocol) {
        return 4;
    }

    const net::ArpPacket arp_request = {
        net::ArpOperation::Request,
        remote_mac,
        remote_ip,
        {{0, 0, 0, 0, 0, 0}},
        local_ip
    };
    size_t arp_length = 0;
    if (net::serialize_arp(
            arp_request,
            packet,
            sizeof(packet),
            &arp_length
        ) != net::Status::Ok ||
        arp_length != net::ARP_PACKET_SIZE) {
        return 5;
    }
    net::ArpPacket parsed_arp = {};
    if (net::parse_arp(packet, arp_length, &parsed_arp) != net::Status::Ok ||
        parsed_arp.operation != net::ArpOperation::Request ||
        !net::mac_equal(parsed_arp.sender_mac, remote_mac) ||
        !net::ipv4_equal(parsed_arp.sender_ip, remote_ip) ||
        !net::ipv4_equal(parsed_arp.target_ip, local_ip)) {
        return 6;
    }
    packet[4] = 5;
    if (net::parse_arp(packet, arp_length, &parsed_arp) !=
        net::Status::MalformedPacket) {
        return 7;
    }
    packet[4] = net::MAC_ADDRESS_LENGTH;
    if (net::parse_arp(packet, net::ARP_PACKET_SIZE - 1, &parsed_arp) !=
        net::Status::FrameTooShort) {
        return 8;
    }

    const uint8_t odd_payload[] = {1, 2, 3, 4, 5};
    size_t icmp_length = 0;
    if (net::serialize_icmp_echo(
            net::IcmpEchoType::Request,
            UINT16_C(0x1234),
            7,
            odd_payload,
            sizeof(odd_payload),
            packet,
            sizeof(packet),
            &icmp_length
        ) != net::Status::Ok ||
        net::internet_checksum(packet, icmp_length) != 0) {
        return 9;
    }
    net::IcmpEchoView echo = {};
    if (net::parse_icmp_echo(packet, icmp_length, &echo) != net::Status::Ok ||
        echo.type != net::IcmpEchoType::Request ||
        echo.identifier != UINT16_C(0x1234) ||
        echo.sequence != 7 ||
        echo.payload_length != sizeof(odd_payload) ||
        !bytes_equal(echo.payload, odd_payload, sizeof(odd_payload))) {
        return 10;
    }
    packet[icmp_length - 1] ^= 1;
    if (net::parse_icmp_echo(packet, icmp_length, &echo) !=
        net::Status::ChecksumMismatch) {
        return 11;
    }
    packet[icmp_length - 1] ^= 1;

    const net::IPv4Header ipv4_header = {
        remote_ip,
        local_ip,
        net::IPV4_PROTOCOL_ICMP,
        64,
        UINT16_C(0x4321),
        true
    };
    size_t ipv4_length = 0;
    if (net::serialize_ipv4(
            ipv4_header,
            packet,
            icmp_length,
            second_packet,
            sizeof(second_packet),
            &ipv4_length
        ) != net::Status::Ok ||
        net::internet_checksum(second_packet, net::IPV4_MIN_HEADER_SIZE) != 0) {
        return 12;
    }
    net::IPv4PacketView ipv4 = {};
    if (net::parse_ipv4(second_packet, ipv4_length, &ipv4) != net::Status::Ok ||
        !net::ipv4_equal(ipv4.source, remote_ip) ||
        !net::ipv4_equal(ipv4.destination, local_ip) ||
        ipv4.protocol != net::IPV4_PROTOCOL_ICMP ||
        ipv4.identification != UINT16_C(0x4321) ||
        !ipv4.dont_fragment ||
        ipv4.payload_length != icmp_length) {
        return 13;
    }

    copy_bytes(packet, second_packet, ipv4_length);
    packet[10] ^= 1;
    if (net::parse_ipv4(packet, ipv4_length, &ipv4) !=
        net::Status::ChecksumMismatch) {
        return 14;
    }
    copy_bytes(packet, second_packet, ipv4_length);
    net::write_be16(packet + 6, UINT16_C(0x2000));
    net::write_be16(packet + 10, 0);
    net::write_be16(
        packet + 10,
        net::internet_checksum(packet, net::IPV4_MIN_HEADER_SIZE)
    );
    if (net::parse_ipv4(packet, ipv4_length, &ipv4) !=
        net::Status::UnsupportedFragment) {
        return 15;
    }

    const net::MacAddress invalid_mac =
        {{0x01, 0x00, 0x00, 0x00, 0x00, 0x01}};
    if (net::initialize_loopback(&loopback, invalid_mac) !=
            net::Status::InvalidArgument ||
        net::initialize_loopback(&loopback, local_mac) != net::Status::Ok) {
        return 16;
    }

    if (net::interface_transmit(
            &loopback.interface,
            frame,
            frame_length
        ) != net::Status::Ok ||
        net::loopback_queued_frames(&loopback) != 1) {
        return 17;
    }
    size_t received_length = 0;
    if (net::interface_receive(
            &loopback.interface,
            second_frame,
            frame_length - 1,
            &received_length
        ) != net::Status::BufferTooSmall ||
        received_length != frame_length ||
        net::loopback_queued_frames(&loopback) != 1 ||
        net::interface_receive(
            &loopback.interface,
            second_frame,
            sizeof(second_frame),
            &received_length
        ) != net::Status::Ok ||
        received_length != frame_length ||
        !bytes_equal(frame, second_frame, frame_length)) {
        return 18;
    }

    for (size_t i = 0; i < net::LOOPBACK_QUEUE_DEPTH; ++i) {
        if (net::interface_transmit(&loopback.interface, frame, frame_length) !=
            net::Status::Ok) {
            return 19;
        }
    }
    if (net::interface_transmit(&loopback.interface, frame, frame_length) !=
            net::Status::QueueFull ||
        loopback.dropped_frames != 1) {
        return 20;
    }
    for (size_t i = 0; i < net::LOOPBACK_QUEUE_DEPTH; ++i) {
        if (net::interface_receive(
                &loopback.interface,
                second_frame,
                sizeof(second_frame),
                &received_length
            ) != net::Status::Ok) {
            return 21;
        }
    }
    if (net::interface_receive(
            &loopback.interface,
            second_frame,
            sizeof(second_frame),
            &received_length
        ) != net::Status::WouldBlock) {
        return 22;
    }

    if (net::initialize_stack(nullptr, &loopback.interface) !=
            net::Status::InvalidArgument ||
        net::initialize_stack(&stack, &loopback.interface) != net::Status::Ok ||
        net::send_ping(
            &stack,
            local_ip,
            1,
            1,
            odd_payload,
            sizeof(odd_payload)
        ) != net::Status::NotConfigured) {
        return 23;
    }

    const net::IPv4Config bad_mask = {
        local_ip,
        {{255, 0, 255, 0}},
        gateway
    };
    const net::IPv4Config bad_host = {
        {{10, 0, 0, 0}},
        netmask,
        gateway
    };
    const net::IPv4Config bad_gateway = {
        local_ip,
        netmask,
        {{10, 0, 1, 1}}
    };
    if (net::configure_ipv4(&stack, bad_mask) !=
            net::Status::InvalidConfiguration ||
        net::configure_ipv4(&stack, bad_host) !=
            net::Status::InvalidConfiguration ||
        net::configure_ipv4(&stack, bad_gateway) !=
            net::Status::InvalidConfiguration) {
        return 24;
    }

    const net::IPv4Config config = {local_ip, netmask, gateway};
    if (net::configure_ipv4(&stack, config) != net::Status::Ok) {
        return 25;
    }
    net::NeighborEntry neighbor = {};
    if (net::lookup_neighbor(&stack, local_ip, &neighbor) != net::Status::Ok ||
        neighbor.state != net::NeighborState::Reachable ||
        !net::mac_equal(neighbor.mac, local_mac)) {
        return 26;
    }
    NeighborListState neighbors = {};
    if (net::list_neighbors(&stack, count_neighbor, &neighbors) != net::Status::Ok ||
        neighbors.count != 1) {
        return 27;
    }

    net::PingReply ping_reply = {};
    if (net::get_last_ping_reply(&stack, &ping_reply) != net::Status::WouldBlock ||
        net::send_ping(
            &stack,
            local_ip,
            UINT16_C(0xbeef),
            9,
            odd_payload,
            sizeof(odd_payload)
        ) != net::Status::Ok ||
        net::loopback_queued_frames(&loopback) != 1) {
        return 28;
    }

    size_t processed = 0;
    if (net::poll(&stack, 4, &processed) != net::Status::Ok ||
        processed != 2 ||
        net::loopback_queued_frames(&loopback) != 0 ||
        net::get_last_ping_reply(&stack, &ping_reply) != net::Status::Ok ||
        !ping_reply.valid ||
        !net::ipv4_equal(ping_reply.source, local_ip) ||
        ping_reply.identifier != UINT16_C(0xbeef) ||
        ping_reply.sequence != 9 ||
        ping_reply.payload_length != sizeof(odd_payload)) {
        return 29;
    }

    net::NetworkStats stats = {};
    if (net::get_stats(&stack, &stats) != net::Status::Ok ||
        stats.frames_received != 2 ||
        stats.frames_transmitted != 2 ||
        stats.ipv4_received != 2 ||
        stats.ipv4_transmitted != 2 ||
        stats.icmp_received != 2 ||
        stats.icmp_transmitted != 2 ||
        stats.echo_requests_sent != 1 ||
        stats.echo_requests_received != 1 ||
        stats.echo_replies_sent != 1 ||
        stats.echo_replies_received != 1 ||
        stats.checksum_errors != 0) {
        return 30;
    }
    if (net::poll(&stack, 4, &processed) != net::Status::Ok || processed != 0) {
        return 31;
    }

    if (make_arp_frame(
            net::ArpOperation::Request,
            broadcast_mac,
            remote_mac,
            remote_ip,
            {{0, 0, 0, 0, 0, 0}},
            local_ip,
            frame,
            sizeof(frame),
            &frame_length
        ) != net::Status::Ok ||
        net::interface_transmit(&loopback.interface, frame, frame_length) !=
            net::Status::Ok ||
        net::poll(&stack, 1, &processed) != net::Status::Ok ||
        processed != 1 ||
        net::loopback_queued_frames(&loopback) != 1) {
        return 32;
    }

    if (net::interface_receive(
            &loopback.interface,
            second_frame,
            sizeof(second_frame),
            &received_length
        ) != net::Status::Ok ||
        net::parse_ethernet(second_frame, received_length, &ethernet) !=
            net::Status::Ok ||
        ethernet.ether_type != net::ETHER_TYPE_ARP ||
        !net::mac_equal(ethernet.destination, remote_mac) ||
        net::parse_arp(ethernet.payload, ethernet.payload_length, &parsed_arp) !=
            net::Status::Ok ||
        parsed_arp.operation != net::ArpOperation::Reply ||
        !net::ipv4_equal(parsed_arp.sender_ip, local_ip) ||
        !net::ipv4_equal(parsed_arp.target_ip, remote_ip)) {
        return 33;
    }
    if (net::lookup_neighbor(&stack, remote_ip, &neighbor) != net::Status::Ok ||
        !net::mac_equal(neighbor.mac, remote_mac)) {
        return 34;
    }

    if (net::send_ping(
            &stack,
            remote_ip,
            5,
            6,
            odd_payload,
            sizeof(odd_payload)
        ) != net::Status::Ok ||
        net::interface_receive(
            &loopback.interface,
            second_frame,
            sizeof(second_frame),
            &received_length
        ) != net::Status::Ok ||
        net::parse_ethernet(second_frame, received_length, &ethernet) !=
            net::Status::Ok ||
        ethernet.ether_type != net::ETHER_TYPE_IPV4 ||
        !net::mac_equal(ethernet.destination, remote_mac) ||
        net::parse_ipv4(ethernet.payload, ethernet.payload_length, &ipv4) !=
            net::Status::Ok ||
        !net::ipv4_equal(ipv4.destination, remote_ip) ||
        net::parse_icmp_echo(ipv4.payload, ipv4.payload_length, &echo) !=
            net::Status::Ok ||
        echo.type != net::IcmpEchoType::Request ||
        echo.identifier != 5 ||
        echo.sequence != 6) {
        return 35;
    }

    const net::IPv4Address unresolved_ip = {{10, 0, 0, 99}};
    if (net::send_ping(
            &stack,
            unresolved_ip,
            1,
            1,
            nullptr,
            0
        ) != net::Status::NeighborResolutionPending ||
        net::interface_receive(
            &loopback.interface,
            second_frame,
            sizeof(second_frame),
            &received_length
        ) != net::Status::Ok ||
        net::parse_ethernet(second_frame, received_length, &ethernet) !=
            net::Status::Ok ||
        ethernet.ether_type != net::ETHER_TYPE_ARP ||
        !net::mac_is_broadcast(ethernet.destination) ||
        net::parse_arp(ethernet.payload, ethernet.payload_length, &parsed_arp) !=
            net::Status::Ok ||
        !net::ipv4_equal(parsed_arp.target_ip, unresolved_ip)) {
        return 36;
    }

    const net::IPv4Config no_gateway = {local_ip, netmask, zero_ip};
    const net::IPv4Address public_ip = {{8, 8, 8, 8}};
    if (net::configure_ipv4(&stack, no_gateway) != net::Status::Ok ||
        net::send_ping(&stack, public_ip, 1, 1, nullptr, 0) !=
            net::Status::NoRoute) {
        return 37;
    }

    if (net::receive(&stack, frame, net::ETHERNET_HEADER_SIZE - 1) !=
            net::Status::FrameTooShort ||
        net::get_stats(&stack, &stats) != net::Status::Ok ||
        stats.parse_errors == 0 ||
        stats.dropped_frames == 0) {
        return 38;
    }

    if (net::configure_ipv4(&stack, config) != net::Status::Ok) {
        return 39;
    }
    for (size_t i = 0; i < net::NEIGHBOR_TABLE_CAPACITY; ++i) {
        const net::MacAddress generated_mac = {
            {0x02, 0x10, 0x00, 0x00, 0x00, static_cast<uint8_t>(i + 1)}
        };
        const net::IPv4Address generated_ip = {
            {10, 0, 0, static_cast<uint8_t>(20 + i)}
        };
        if (make_arp_frame(
                net::ArpOperation::Reply,
                local_mac,
                generated_mac,
                generated_ip,
                local_mac,
                local_ip,
                frame,
                sizeof(frame),
                &frame_length
            ) != net::Status::Ok ||
            net::receive(&stack, frame, frame_length) != net::Status::Ok) {
            return 40;
        }
    }
    if (net::get_stats(&stack, &stats) != net::Status::Ok ||
        stats.neighbor_evictions == 0 ||
        net::lookup_neighbor(&stack, local_ip, &neighbor) != net::Status::Ok ||
        !net::mac_equal(neighbor.mac, local_mac)) {
        return 41;
    }

    neighbors = {};
    if (net::list_neighbors(&stack, stop_neighbor_list, &neighbors) !=
            net::Status::IterationStopped ||
        neighbors.count != 1 ||
        net::list_neighbors(&stack, nullptr, nullptr) !=
            net::Status::InvalidArgument ||
        net::get_stats(&stack, nullptr) != net::Status::InvalidArgument ||
        net::get_last_ping_reply(&stack, nullptr) !=
            net::Status::InvalidArgument) {
        return 42;
    }

    if (net::send_ping(
            &stack,
            zero_ip,
            1,
            1,
            nullptr,
            0
        ) != net::Status::InvalidArgument ||
        net::send_ping(
            &stack,
            local_ip,
            1,
            1,
            odd_payload,
            net::ICMP_ECHO_MAX_PAYLOAD + 1
        ) != net::Status::PayloadTooLarge) {
        return 43;
    }

    if (!text_equals(
            net::status_message(net::Status::NeighborResolutionPending),
            "neighbor resolution pending"
        )) {
        return 44;
    }

    return 0;
}

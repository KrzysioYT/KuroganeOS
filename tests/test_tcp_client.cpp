#include "../kernel/net/tcp_client.hpp"
#include "../kernel/net/protocols.hpp"

#include <stddef.h>
#include <stdint.h>

namespace {

constexpr size_t kInboundCapacity = 16U;
constexpr size_t kSentCapacity = 32U;

struct SentSegment {
    uint32_t sequence;
    uint32_t acknowledgement;
    uint8_t flags;
    size_t payload_length;
    uint8_t payload[net::tcp_client::MAX_SEGMENT_PAYLOAD];
};

net::TcpSegment g_inbound[kInboundCapacity]{};
size_t g_inbound_head = 0U;
size_t g_inbound_tail = 0U;
SentSegment g_sent[kSentCapacity]{};
size_t g_sent_count = 0U;
uint64_t g_ticks = 0U;
uint32_t g_server_sequence = UINT32_C(1000);
bool g_ack_first_data_transmission = true;
size_t g_data_transmissions = 0U;

const net::IPv4Address kLocalIp = {{10, 0, 2, 15}};
const net::IPv4Address kPeerIp = {{93, 184, 216, 34}};
constexpr uint16_t kLocalPort = UINT16_C(49152);
constexpr uint16_t kRemotePort = UINT16_C(443);

bool bytes_equal(const uint8_t* left, const uint8_t* right, size_t length) {
    for (size_t index = 0U; index < length; ++index) {
        if (left[index] != right[index]) return false;
    }
    return true;
}

void reset_wire() {
    g_inbound_head = 0U;
    g_inbound_tail = 0U;
    g_sent_count = 0U;
    g_ticks = 0U;
    g_server_sequence = UINT32_C(1000);
    g_ack_first_data_transmission = true;
    g_data_transmissions = 0U;
}

bool enqueue_inbound(const net::TcpSegment& segment) {
    if (g_inbound_tail >= kInboundCapacity) return false;
    g_inbound[g_inbound_tail++] = segment;
    return true;
}

net::TcpSegment make_peer_segment(
    uint32_t sequence,
    uint32_t acknowledgement,
    uint8_t flags,
    const uint8_t* payload,
    size_t payload_length) {
    net::TcpSegment segment{};
    segment.valid = true;
    segment.source = kPeerIp;
    segment.destination = kLocalIp;
    segment.source_port = kRemotePort;
    segment.destination_port = kLocalPort;
    segment.sequence = sequence;
    segment.acknowledgement = acknowledgement;
    segment.flags = flags;
    segment.window = UINT16_C(8192);
    segment.payload_length = payload_length;
    for (size_t index = 0U; index < payload_length; ++index) {
        segment.payload[index] = payload[index];
    }
    return segment;
}

bool connect_client(net::tcp_client::Client* client, net::NetworkStack* stack) {
    stack->initialized = true;
    stack->ipv4_configured = true;
    stack->config.address = kLocalIp;
    return net::tcp_client::connect(
               client,
               stack,
               kPeerIp,
               kLocalPort,
               kRemotePort,
               UINT32_C(77),
               UINT64_C(1000)) == net::Status::Ok;
}

} // namespace

namespace drivers::pit {

bool initialize(uint32_t) { return true; }
void shutdown() {}
bool initialized() { return true; }
uint32_t frequency_hz() { return UINT32_C(1000); }
uint16_t divisor() { return 1U; }
uint64_t ticks() { return g_ticks++; }
void reset_ticks() { g_ticks = 0U; }
void handle_irq() {}

} // namespace drivers::pit

namespace net {

bool ipv4_equal(const IPv4Address& left, const IPv4Address& right) {
    for (size_t index = 0U; index < IPV4_ADDRESS_LENGTH; ++index) {
        if (left.bytes[index] != right.bytes[index]) return false;
    }
    return true;
}

bool ipv4_is_zero(const IPv4Address& address) {
    for (size_t index = 0U; index < IPV4_ADDRESS_LENGTH; ++index) {
        if (address.bytes[index] != 0U) return false;
    }
    return true;
}

Status poll(NetworkStack*, size_t, size_t* out_processed) {
    if (out_processed != nullptr) *out_processed = 0U;
    return Status::Ok;
}

Status take_tcp_segment(NetworkStack*, TcpSegment* out_segment) {
    if (out_segment == nullptr) return Status::InvalidArgument;
    if (g_inbound_head >= g_inbound_tail) return Status::WouldBlock;
    *out_segment = g_inbound[g_inbound_head++];
    return Status::Ok;
}

Status send_tcp(
    NetworkStack*,
    const IPv4Address& destination,
    uint16_t source_port,
    uint16_t destination_port,
    uint32_t sequence,
    uint32_t acknowledgement,
    uint8_t flags,
    uint16_t,
    const uint8_t* payload,
    size_t payload_length) {
    if (!ipv4_equal(destination, kPeerIp) || source_port != kLocalPort ||
        destination_port != kRemotePort || g_sent_count >= kSentCapacity ||
        payload_length > tcp_client::MAX_SEGMENT_PAYLOAD) {
        return Status::InvalidArgument;
    }

    SentSegment& sent = g_sent[g_sent_count++];
    sent.sequence = sequence;
    sent.acknowledgement = acknowledgement;
    sent.flags = flags;
    sent.payload_length = payload_length;
    for (size_t index = 0U; index < payload_length; ++index) {
        sent.payload[index] = payload[index];
    }

    if ((flags & TcpSyn) != 0U) {
        const TcpSegment syn_ack = make_peer_segment(
            g_server_sequence,
            sequence + 1U,
            TcpSyn | TcpAck,
            nullptr,
            0U);
        if (!enqueue_inbound(syn_ack)) return Status::QueueFull;
        ++g_server_sequence;
        return Status::Ok;
    }

    if (payload_length != 0U) {
        ++g_data_transmissions;
        if (g_ack_first_data_transmission || g_data_transmissions >= 2U) {
            const TcpSegment ack = make_peer_segment(
                g_server_sequence,
                sequence + static_cast<uint32_t>(payload_length),
                TcpAck,
                nullptr,
                0U);
            if (!enqueue_inbound(ack)) return Status::QueueFull;
        }
    }
    return Status::Ok;
}

} // namespace net

int main() {
    // Retransmission regression: if the first ACK is lost, the second wire
    // transmission must carry the same application bytes at the same SEQ.
    reset_wire();
    net::NetworkStack retransmit_stack{};
    net::tcp_client::Client retransmit_client{};
    if (!connect_client(&retransmit_client, &retransmit_stack)) return 1;

    g_ack_first_data_transmission = false;
    const uint8_t request[] = {'T', 'L', 'S'};
    const size_t sent_before_data = g_sent_count;
    if (net::tcp_client::send(
            &retransmit_client,
            request,
            sizeof(request),
            UINT64_C(900)) != net::Status::Ok) {
        return 2;
    }
    if (g_sent_count < sent_before_data + 2U) return 3;
    const SentSegment& first = g_sent[sent_before_data];
    const SentSegment& retry = g_sent[sent_before_data + 1U];
    if (first.sequence != retry.sequence ||
        first.payload_length != sizeof(request) ||
        retry.payload_length != sizeof(request) ||
        !bytes_equal(first.payload, request, sizeof(request)) ||
        !bytes_equal(retry.payload, request, sizeof(request)) ||
        retransmit_client.send_unacknowledged !=
            first.sequence + static_cast<uint32_t>(sizeof(request)) ||
        retransmit_client.send_next !=
            first.sequence + static_cast<uint32_t>(sizeof(request))) {
        return 4;
    }

    // Reassembly regression: deliver the second half first. receive() must
    // retain it, ACK the gap, then merge it after the first half arrives.
    reset_wire();
    net::NetworkStack reorder_stack{};
    net::tcp_client::Client reorder_client{};
    if (!connect_client(&reorder_client, &reorder_stack)) return 5;

    const uint32_t base = reorder_client.receive_next;
    const uint8_t first_half[] = {'h', 'e', 'l', 'l', 'o'};
    const uint8_t second_half[] = {'w', 'o', 'r', 'l', 'd'};
    const net::TcpSegment later = make_peer_segment(
        base + static_cast<uint32_t>(sizeof(first_half)),
        reorder_client.send_next,
        net::TcpAck | net::TcpPsh,
        second_half,
        sizeof(second_half));
    const net::TcpSegment earlier = make_peer_segment(
        base,
        reorder_client.send_next,
        net::TcpAck | net::TcpPsh,
        first_half,
        sizeof(first_half));
    if (!enqueue_inbound(later) || !enqueue_inbound(earlier)) return 6;

    uint8_t output[16]{};
    size_t output_length = 0U;
    if (net::tcp_client::receive(
            &reorder_client,
            output,
            sizeof(output),
            &output_length,
            UINT64_C(1000)) != net::Status::Ok ||
        output_length != sizeof(first_half) + sizeof(second_half)) {
        return 7;
    }
    const uint8_t expected[] = {
        'h', 'e', 'l', 'l', 'o', 'w', 'o', 'r', 'l', 'd'};
    if (!bytes_equal(output, expected, sizeof(expected)) ||
        reorder_client.receive_next != base + sizeof(expected) ||
        reorder_client.out_of_order_count != 0U) {
        return 8;
    }

    return 0;
}

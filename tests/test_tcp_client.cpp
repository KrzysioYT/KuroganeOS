#include "../kernel/net/tcp_client.hpp"
#include "../kernel/net/protocols.hpp"

#include <stddef.h>
#include <stdint.h>

namespace {

constexpr size_t kInboundCapacity = 24U;
constexpr size_t kSentCapacity = 64U;

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
bool g_fail_first_data_transmission = false;
bool g_failed_data_transmission = false;
bool g_fail_next_pure_ack = false;
bool g_tls_like_ack_payload = false;
bool g_zero_window_syn_ack = false;
bool g_rst_on_data = false;
bool g_never_ack_data = false;

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
    g_fail_first_data_transmission = false;
    g_failed_data_transmission = false;
    g_fail_next_pure_ack = false;
    g_tls_like_ack_payload = false;
    g_zero_window_syn_ack = false;
    g_rst_on_data = false;
    g_never_ack_data = false;
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
    size_t payload_length,
    uint16_t window = UINT16_C(8192)) {
    net::TcpSegment segment{};
    segment.valid = true;
    segment.source = kPeerIp;
    segment.destination = kLocalIp;
    segment.source_port = kRemotePort;
    segment.destination_port = kLocalPort;
    segment.sequence = sequence;
    segment.acknowledgement = acknowledgement;
    segment.flags = flags;
    segment.window = window;
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
            0U,
            g_zero_window_syn_ack ? 0U : UINT16_C(8192));
        if (!enqueue_inbound(syn_ack)) return Status::QueueFull;
        ++g_server_sequence;
        return Status::Ok;
    }

    if (payload_length == 0U && (flags & TcpAck) != 0U &&
        (flags & (TcpSyn | TcpFin)) == 0U) {
        if (g_fail_next_pure_ack) {
            g_fail_next_pure_ack = false;
            return Status::InterfaceError;
        }
        return Status::Ok;
    }

    if (payload_length != 0U) {
        ++g_data_transmissions;

        if (g_fail_first_data_transmission && !g_failed_data_transmission) {
            g_failed_data_transmission = true;
            return Status::InterfaceError;
        }

        if (g_rst_on_data) {
            const TcpSegment rst = make_peer_segment(
                g_server_sequence,
                sequence,
                TcpRst | TcpAck,
                nullptr,
                0U);
            if (!enqueue_inbound(rst)) return Status::QueueFull;
            return Status::Ok;
        }

        if (g_tls_like_ack_payload) {
            static const uint8_t server_hello[] = {'S', 'E', 'R', 'V', 'E', 'R'};
            const TcpSegment reply = make_peer_segment(
                g_server_sequence,
                sequence + static_cast<uint32_t>(payload_length),
                TcpAck | TcpPsh,
                server_hello,
                sizeof(server_hello));
            if (!enqueue_inbound(reply)) return Status::QueueFull;
            g_server_sequence += static_cast<uint32_t>(sizeof(server_hello));
            return Status::Ok;
        }

        if (!g_never_ack_data &&
            (g_ack_first_data_transmission || g_data_transmissions >= 2U)) {
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

    // Local NIC backpressure regression: an InterfaceError from the local TX
    // boundary may represent a temporarily busy descriptor. Retry the exact
    // same SEQ and payload instead of poisoning the connection immediately.
    reset_wire();
    net::NetworkStack tx_stall_stack{};
    net::tcp_client::Client tx_stall_client{};
    if (!connect_client(&tx_stall_client, &tx_stall_stack)) return 9;
    g_fail_first_data_transmission = true;
    const size_t stall_before = g_sent_count;
    if (net::tcp_client::send(
            &tx_stall_client,
            request,
            sizeof(request),
            UINT64_C(1000)) != net::Status::Ok) {
        return 10;
    }
    if (!g_failed_data_transmission || g_sent_count < stall_before + 2U) return 11;
    const SentSegment& stalled = g_sent[stall_before];
    const SentSegment& recovered = g_sent[stall_before + 1U];
    if (stalled.sequence != recovered.sequence ||
        stalled.payload_length != recovered.payload_length ||
        !bytes_equal(stalled.payload, recovered.payload, stalled.payload_length)) {
        return 12;
    }

    // TLS-like server flight regression: a server commonly ACKs ClientHello
    // and sends ServerHello in the same segment. If our pure ACK transmission
    // momentarily fails, the already-valid incoming payload must stay buffered
    // and the connection must remain established.
    reset_wire();
    net::NetworkStack tls_stack{};
    net::tcp_client::Client tls_client{};
    if (!connect_client(&tls_client, &tls_stack)) return 13;
    g_tls_like_ack_payload = true;
    g_fail_next_pure_ack = true;
    const uint8_t client_hello[] = {'C', 'L', 'I', 'E', 'N', 'T'};
    if (net::tcp_client::send(
            &tls_client,
            client_hello,
            sizeof(client_hello),
            UINT64_C(1000)) != net::Status::Ok ||
        !tls_client.connected ||
        tls_client.state != net::tcp_client::State::Established) {
        return 14;
    }
    uint8_t tls_output[16]{};
    size_t tls_output_length = 0U;
    if (net::tcp_client::receive(
            &tls_client,
            tls_output,
            sizeof(tls_output),
            &tls_output_length,
            UINT64_C(1000)) != net::Status::Ok) {
        return 15;
    }
    static const uint8_t expected_server_hello[] = {'S', 'E', 'R', 'V', 'E', 'R'};
    if (tls_output_length != sizeof(expected_server_hello) ||
        !bytes_equal(tls_output, expected_server_hello, sizeof(expected_server_hello))) {
        return 16;
    }

    // RFC window regression: a zero peer window is not permission to transmit.
    // Return WouldBlock without changing SND.NXT so a later window update can
    // resume safely.
    reset_wire();
    g_zero_window_syn_ack = true;
    net::NetworkStack zero_window_stack{};
    net::tcp_client::Client zero_window_client{};
    if (!connect_client(&zero_window_client, &zero_window_stack)) return 17;
    const uint32_t zero_window_seq = zero_window_client.send_next;
    const size_t zero_window_before = g_data_transmissions;
    if (net::tcp_client::send(
            &zero_window_client,
            request,
            sizeof(request),
            UINT64_C(1000)) != net::Status::WouldBlock ||
        zero_window_client.send_next != zero_window_seq ||
        g_data_transmissions != zero_window_before) {
        return 18;
    }

    // Peer reset remains fatal. The local-TX retry policy must never hide a RST
    // received from the remote endpoint.
    reset_wire();
    net::NetworkStack rst_stack{};
    net::tcp_client::Client rst_client{};
    if (!connect_client(&rst_client, &rst_stack)) return 19;
    g_rst_on_data = true;
    if (net::tcp_client::send(
            &rst_client,
            request,
            sizeof(request),
            UINT64_C(1000)) != net::Status::InterfaceError ||
        rst_client.state != net::tcp_client::State::Error ||
        rst_client.connected) {
        return 20;
    }

    // ACK-deadline regression: if no ACK arrives for an entire queued segment,
    // preserve the established connection, rewind SND.NXT to the segment's
    // original sequence, and return WouldBlock. A subsequent nonblocking BIO
    // retry must submit the identical plaintext at the identical sequence and
    // complete normally once the ACK path recovers.
    reset_wire();
    net::NetworkStack ack_timeout_stack{};
    net::tcp_client::Client ack_timeout_client{};
    if (!connect_client(&ack_timeout_client, &ack_timeout_stack)) return 21;
    g_never_ack_data = true;
    const uint32_t retry_sequence = ack_timeout_client.send_next;
    const size_t timeout_sent_before = g_sent_count;
    if (net::tcp_client::send(
            &ack_timeout_client,
            request,
            sizeof(request),
            UINT64_C(1000)) != net::Status::WouldBlock ||
        !ack_timeout_client.connected ||
        ack_timeout_client.state != net::tcp_client::State::Established ||
        ack_timeout_client.send_next != retry_sequence ||
        ack_timeout_client.send_unacknowledged != retry_sequence) {
        return 22;
    }
    if (g_sent_count <= timeout_sent_before) return 23;
    for (size_t index = timeout_sent_before; index < g_sent_count; ++index) {
        if (g_sent[index].payload_length != sizeof(request) ||
            g_sent[index].sequence != retry_sequence ||
            !bytes_equal(g_sent[index].payload, request, sizeof(request))) {
            return 24;
        }
    }

    g_never_ack_data = false;
    const size_t recovered_retry_index = g_sent_count;
    if (net::tcp_client::send(
            &ack_timeout_client,
            request,
            sizeof(request),
            UINT64_C(1000)) != net::Status::Ok ||
        !ack_timeout_client.connected ||
        ack_timeout_client.state != net::tcp_client::State::Established ||
        ack_timeout_client.send_next !=
            retry_sequence + static_cast<uint32_t>(sizeof(request)) ||
        ack_timeout_client.send_unacknowledged !=
            retry_sequence + static_cast<uint32_t>(sizeof(request))) {
        return 25;
    }
    if (recovered_retry_index >= g_sent_count ||
        g_sent[recovered_retry_index].sequence != retry_sequence ||
        g_sent[recovered_retry_index].payload_length != sizeof(request) ||
        !bytes_equal(
            g_sent[recovered_retry_index].payload,
            request,
            sizeof(request))) {
        return 26;
    }

    return 0;
}
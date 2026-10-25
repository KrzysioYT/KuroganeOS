#include "../kernel/net/tcp_client.hpp"
#include "../kernel/net/protocols.hpp"

#include <stddef.h>
#include <stdint.h>

namespace {

constexpr size_t kInboundCapacity = 16U;
constexpr size_t kSentCapacity = 32U;

struct SentSegment {
    uint32_t sequence;
    uint8_t flags;
};

net::TcpSegment g_inbound[kInboundCapacity]{};
size_t g_inbound_head = 0U;
size_t g_inbound_tail = 0U;
SentSegment g_sent[kSentCapacity]{};
size_t g_sent_count = 0U;
uint64_t g_ticks = 0U;
uint32_t g_server_sequence = UINT32_C(7000);
size_t g_fin_transmissions = 0U;
size_t g_rst_transmissions = 0U;
bool g_fail_first_fin = false;
bool g_failed_first_fin = false;
bool g_fail_all_close_tx = false;
bool g_peer_close_response = true;
bool g_peer_already_closed = false;

const net::IPv4Address kLocalIp = {{10, 0, 2, 15}};
const net::IPv4Address kPeerIp = {{93, 184, 216, 34}};
constexpr uint16_t kLocalPort = UINT16_C(49153);
constexpr uint16_t kRemotePort = UINT16_C(443);

bool enqueue_inbound(const net::TcpSegment& segment) {
    if (g_inbound_tail >= kInboundCapacity) return false;
    g_inbound[g_inbound_tail++] = segment;
    return true;
}

net::TcpSegment make_peer_segment(
    uint32_t sequence,
    uint32_t acknowledgement,
    uint8_t flags) {
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
    return segment;
}

void reset_wire() {
    g_inbound_head = 0U;
    g_inbound_tail = 0U;
    g_sent_count = 0U;
    g_ticks = 0U;
    g_server_sequence = UINT32_C(7000);
    g_fin_transmissions = 0U;
    g_rst_transmissions = 0U;
    g_fail_first_fin = false;
    g_failed_first_fin = false;
    g_fail_all_close_tx = false;
    g_peer_close_response = true;
    g_peer_already_closed = false;
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
               UINT32_C(123),
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
    uint32_t,
    uint8_t flags,
    uint16_t,
    const uint8_t*,
    size_t payload_length) {
    if (!ipv4_equal(destination, kPeerIp) || source_port != kLocalPort ||
        destination_port != kRemotePort || payload_length != 0U ||
        g_sent_count >= kSentCapacity) {
        return Status::InvalidArgument;
    }

    g_sent[g_sent_count++] = {sequence, flags};

    if ((flags & TcpSyn) != 0U) {
        if (!enqueue_inbound(make_peer_segment(
                g_server_sequence,
                sequence + 1U,
                TcpSyn | TcpAck))) {
            return Status::QueueFull;
        }
        ++g_server_sequence;
        return Status::Ok;
    }

    if ((flags & TcpRst) != 0U) {
        ++g_rst_transmissions;
        return g_fail_all_close_tx ? Status::InterfaceError : Status::Ok;
    }

    if ((flags & TcpFin) != 0U) {
        ++g_fin_transmissions;
        if (g_fail_all_close_tx) return Status::InterfaceError;
        if (g_fail_first_fin && !g_failed_first_fin) {
            g_failed_first_fin = true;
            return Status::InterfaceError;
        }
        if (!g_peer_close_response) return Status::Ok;

        const uint8_t response_flags = g_peer_already_closed
            ? TcpAck
            : static_cast<uint8_t>(TcpAck | TcpFin);
        if (!enqueue_inbound(make_peer_segment(
                g_server_sequence,
                sequence + 1U,
                response_flags))) {
            return Status::QueueFull;
        }
        if (!g_peer_already_closed) ++g_server_sequence;
        return Status::Ok;
    }

    return Status::Ok;
}

} // namespace net

int main() {
    // Normal active close: FIN is ACKed and peer FIN is consumed before local
    // state is released.
    reset_wire();
    net::NetworkStack normal_stack{};
    net::tcp_client::Client normal_client{};
    if (!connect_client(&normal_client, &normal_stack)) return 1;
    if (net::tcp_client::close(&normal_client) != net::Status::Ok ||
        normal_client.connected ||
        normal_client.state != net::tcp_client::State::Closed ||
        g_fin_transmissions == 0U || g_rst_transmissions != 0U) {
        return 2;
    }

    // Local TX backpressure must retransmit the same FIN sequence rather than
    // reporting success without ever queueing a close segment.
    reset_wire();
    net::NetworkStack retry_stack{};
    net::tcp_client::Client retry_client{};
    if (!connect_client(&retry_client, &retry_stack)) return 3;
    g_fail_first_fin = true;
    const size_t retry_start = g_sent_count;
    if (net::tcp_client::close(&retry_client) != net::Status::Ok ||
        !g_failed_first_fin || g_fin_transmissions < 2U ||
        retry_client.state != net::tcp_client::State::Closed) {
        return 4;
    }
    uint32_t first_fin_sequence = 0U;
    bool have_first_fin = false;
    for (size_t index = retry_start; index < g_sent_count; ++index) {
        if ((g_sent[index].flags & net::TcpFin) == 0U) continue;
        if (!have_first_fin) {
            first_fin_sequence = g_sent[index].sequence;
            have_first_fin = true;
        } else if (g_sent[index].sequence != first_fin_sequence) {
            return 5;
        }
    }

    // A peer that ACKs nothing after FIN is actively aborted with a real RST;
    // only after that RST is queued may close() return success and release state.
    reset_wire();
    net::NetworkStack abort_stack{};
    net::tcp_client::Client abort_client{};
    if (!connect_client(&abort_client, &abort_stack)) return 6;
    g_peer_close_response = false;
    if (net::tcp_client::close(&abort_client) != net::Status::Ok ||
        g_fin_transmissions == 0U || g_rst_transmissions == 0U ||
        abort_client.state != net::tcp_client::State::Closed) {
        return 7;
    }

    // If neither FIN nor abort can be queued, failure must stay visible and
    // the client must not be silently reset to Closed.
    reset_wire();
    net::NetworkStack blocked_stack{};
    net::tcp_client::Client blocked_client{};
    if (!connect_client(&blocked_client, &blocked_stack)) return 8;
    g_fail_all_close_tx = true;
    const net::Status blocked_status = net::tcp_client::close(&blocked_client);
    if (blocked_status == net::Status::Ok || !blocked_client.connected ||
        blocked_client.state == net::tcp_client::State::Closed) {
        return 9;
    }

    // Peer-initiated close enters CLOSE-WAIT. Local close then sends the final
    // FIN and waits only for its ACK because the remote FIN was already seen.
    reset_wire();
    net::NetworkStack close_wait_stack{};
    net::tcp_client::Client close_wait_client{};
    if (!connect_client(&close_wait_client, &close_wait_stack)) return 10;
    const uint32_t peer_fin_sequence = close_wait_client.receive_next;
    if (!enqueue_inbound(make_peer_segment(
            peer_fin_sequence,
            close_wait_client.send_next,
            net::TcpFin | net::TcpAck))) {
        return 11;
    }
    uint8_t byte = 0U;
    size_t received = 0U;
    if (net::tcp_client::receive(
            &close_wait_client,
            &byte,
            1U,
            &received,
            UINT64_C(1000)) != net::Status::Ok ||
        received != 0U || !close_wait_client.peer_closed ||
        close_wait_client.state != net::tcp_client::State::CloseWait) {
        return 12;
    }
    g_peer_already_closed = true;
    if (net::tcp_client::close(&close_wait_client) != net::Status::Ok ||
        close_wait_client.state != net::tcp_client::State::Closed ||
        close_wait_client.connected) {
        return 13;
    }

    return 0;
}

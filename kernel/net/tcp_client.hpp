#pragma once

#include "network.hpp"

#include <stddef.h>
#include <stdint.h>

namespace net::tcp_client {

constexpr size_t MAX_SEGMENT_PAYLOAD = 1400U;
constexpr size_t STREAM_RECEIVE_CAPACITY = 8U * 1024U;
constexpr size_t OUT_OF_ORDER_QUEUE_DEPTH = 4U;
constexpr size_t OUT_OF_ORDER_PAYLOAD_CAPACITY = TRANSPORT_INBOX_CAPACITY;

enum class State : uint8_t {
    Closed = 0,
    SynSent,
    Established,
    CloseWait,
    FinWait1,
    Reset,
    Error,
};

struct BufferedSegment {
    uint32_t sequence;
    uint8_t flags;
    uint16_t window;
    size_t payload_length;
    uint8_t payload[OUT_OF_ORDER_PAYLOAD_CAPACITY];
    bool valid;
};

struct Client {
    NetworkStack* stack;
    IPv4Address peer;
    uint16_t local_port;
    uint16_t remote_port;

    // RFC-style sequence-space split. send_unacknowledged is SND.UNA,
    // send_next is SND.NXT and receive_next is RCV.NXT.
    uint32_t send_unacknowledged;
    uint32_t send_next;
    uint32_t receive_next;
    uint16_t peer_window;

    // In-order byte stream ready for the consumer.
    uint8_t pending[STREAM_RECEIVE_CAPACITY];
    size_t pending_offset;
    size_t pending_length;

    // Bounded reassembly queue for segments that arrive ahead of RCV.NXT.
    BufferedSegment out_of_order[OUT_OF_ORDER_QUEUE_DEPTH];
    size_t out_of_order_count;

    State state;
    bool connected;
    bool peer_closed;
};

void initialize(Client* client);

/* Starts a TCP three-way handshake without waiting for completion. */
Status begin_connect(
    Client* client,
    NetworkStack* stack,
    const IPv4Address& peer,
    uint16_t local_port,
    uint16_t remote_port,
    uint32_t initial_sequence);

/* Processes at most one transport progression step. */
Status progress(Client* client);

/* Queues at most one bounded TCP payload segment without waiting for ACK. */
Status try_send(
    Client* client,
    const uint8_t* data,
    size_t length,
    size_t* out_sent);

/* Drains available bytes, performing at most one transport progression step. */
Status try_receive(
    Client* client,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length);

/* Starts a graceful FIN close without waiting for peer completion. */
Status begin_close(Client* client);

Status connect(
    Client* client,
    NetworkStack* stack,
    const IPv4Address& peer,
    uint16_t local_port,
    uint16_t remote_port,
    uint32_t initial_sequence,
    uint64_t timeout_ms);

/*
 * Sends the entire buffer in bounded TCP payload chunks. Unacknowledged data
 * is retransmitted with the original sequence number; a retry never creates a
 * second copy of the application bytes at a new sequence number.
 */
Status send(
    Client* client,
    const uint8_t* data,
    size_t length,
    uint64_t timeout_ms);

/*
 * Returns at least one byte when data is available, up to output_capacity.
 * In-order bytes are delivered from the stream queue. Out-of-order segments
 * are retained and merged when the missing sequence range arrives.
 */
Status receive(
    Client* client,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length,
    uint64_t timeout_ms);

Status close(Client* client);

} // namespace net::tcp_client

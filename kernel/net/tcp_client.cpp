#include "tcp_client.hpp"

#include "protocols.hpp"
#include "../arch/x86_64/io.hpp"
#include "../drivers/pit.hpp"
#if !defined(KUROGANE_HOST_TEST)
#include "../core/log.hpp"
#endif

namespace net::tcp_client {
namespace {

constexpr uint16_t kMaximumAdvertisedWindow = UINT16_C(32768);
constexpr size_t kFallbackPollBudget = 800000U;
constexpr size_t kDataTransmissionLimit = 4U;

uint64_t timeout_ticks(uint64_t milliseconds) {
    if (!drivers::pit::initialized()) return 0U;
    const uint64_t frequency = drivers::pit::frequency_hz();
    if (frequency == 0U) return 0U;
    if (milliseconds > (UINT64_MAX - UINT64_C(999)) / frequency) {
        return UINT64_MAX;
    }
    const uint64_t product = milliseconds * frequency;
    const uint64_t ticks = (product + UINT64_C(999)) / UINT64_C(1000);
    return ticks == 0U ? 1U : ticks;
}

bool window_open(
    uint64_t started,
    uint64_t timeout_ms,
    size_t attempt) {
    const uint64_t limit = timeout_ticks(timeout_ms);
    if (limit != 0U && limit != UINT64_MAX) {
        return drivers::pit::ticks() - started < limit;
    }
    return attempt < kFallbackPollBudget;
}

void wait_for_progress() {
#if defined(KUROGANE_HOST_TEST)
    arch::pause();
#else
    const bool interrupts_enabled =
        (arch::read_flags() & (UINT64_C(1) << 9U)) != 0U;
    if (drivers::pit::initialized() && interrupts_enabled) {
        arch::halt();
    } else {
        arch::pause();
    }
#endif
}

bool sequence_before(uint32_t left, uint32_t right) {
    return static_cast<int32_t>(left - right) < 0;
}

bool sequence_after(uint32_t left, uint32_t right) {
    return sequence_before(right, left);
}

bool segment_for_client(const Client& client, const TcpSegment& segment) {
    return segment.valid && ipv4_equal(segment.source, client.peer) &&
        segment.source_port == client.remote_port &&
        segment.destination_port == client.local_port;
}

bool retryable_transmit_status(Status status) {
    // The physical drivers use WouldBlock/QueueFull for a busy descriptor ring.
    // interface_transmit historically collapsed some driver backpressure into
    // InterfaceError, so keep InterfaceError retryable only at the local TX
    // boundary. A received RST is still handled separately and remains fatal.
    return status == Status::NeighborResolutionPending ||
        status == Status::WouldBlock ||
        status == Status::QueueFull ||
        status == Status::InterfaceError;
}

bool acceptable_poll_status(Status status) {
    return status == Status::Ok || status == Status::NotForUs ||
        status == Status::UnsupportedProtocol || status == Status::WouldBlock ||
        status == Status::InterfaceError;
}

size_t buffered_out_of_order_bytes(const Client& client) {
    size_t total = 0U;
    for (size_t index = 0U; index < OUT_OF_ORDER_QUEUE_DEPTH; ++index) {
        if (!client.out_of_order[index].valid) continue;
        const size_t length = client.out_of_order[index].payload_length;
        if (length > SIZE_MAX - total) return SIZE_MAX;
        total += length;
    }
    return total;
}

uint16_t advertised_window(const Client& client) {
    size_t used = client.pending_length;
    const size_t reordered = buffered_out_of_order_bytes(client);
    if (reordered > SIZE_MAX - used) return 0U;
    used += reordered;
    if (used >= STREAM_RECEIVE_CAPACITY) return 0U;
    const size_t free_bytes = STREAM_RECEIVE_CAPACITY - used;
    return static_cast<uint16_t>(
        free_bytes < kMaximumAdvertisedWindow
            ? free_bytes
            : kMaximumAdvertisedWindow);
}

void compact_pending(Client* client) {
    if (client == nullptr || client->pending_offset == 0U) return;
    if (client->pending_length == 0U) {
        client->pending_offset = 0U;
        return;
    }
    for (size_t index = 0U; index < client->pending_length; ++index) {
        client->pending[index] = client->pending[client->pending_offset + index];
    }
    client->pending_offset = 0U;
}

Status append_pending(
    Client* client,
    const uint8_t* data,
    size_t length) {
    if (client == nullptr || (length != 0U && data == nullptr)) {
        return Status::InvalidArgument;
    }
    if (length == 0U) return Status::Ok;
    if (client->pending_offset > 0U &&
        client->pending_offset + client->pending_length + length >
            STREAM_RECEIVE_CAPACITY) {
        compact_pending(client);
    }
    const size_t end = client->pending_offset + client->pending_length;
    if (end > STREAM_RECEIVE_CAPACITY ||
        length > STREAM_RECEIVE_CAPACITY - end) {
        return Status::QueueFull;
    }
    for (size_t index = 0U; index < length; ++index) {
        client->pending[end + index] = data[index];
    }
    client->pending_length += length;
    return Status::Ok;
}

Status acknowledge(Client* client) {
    if (client == nullptr || client->stack == nullptr) {
        return Status::InvalidArgument;
    }
    return send_tcp(
        client->stack,
        client->peer,
        client->local_port,
        client->remote_port,
        client->send_next,
        client->receive_next,
        TcpAck,
        advertised_window(*client),
        nullptr,
        0U);
}

Status acknowledge_or_defer(Client* client) {
    const Status status = acknowledge(client);
    // A pure ACK is safe to defer. If the local NIC is momentarily busy, the
    // peer will retransmit and a later ACK will advertise the same RCV.NXT.
    // Do not tear down a valid TCP/TLS session because one ACK descriptor was
    // temporarily unavailable.
    return retryable_transmit_status(status) ? Status::Ok : status;
}

void record_ack(Client* client, const TcpSegment& segment) {
    if (client == nullptr || (segment.flags & TcpAck) == 0U) return;
    const uint32_t acknowledgement = segment.acknowledgement;
    if (sequence_after(acknowledgement, client->send_next)) {
        return;
    }
    if (sequence_after(acknowledgement, client->send_unacknowledged)) {
        client->send_unacknowledged = acknowledgement;
    }
}

uint32_t buffered_end_sequence(const BufferedSegment& segment) {
    uint32_t end = segment.sequence + static_cast<uint32_t>(segment.payload_length);
    if ((segment.flags & TcpFin) != 0U) ++end;
    return end;
}

uint32_t incoming_end_sequence(const TcpSegment& segment) {
    uint32_t end = segment.sequence + static_cast<uint32_t>(segment.payload_length);
    if ((segment.flags & TcpFin) != 0U) ++end;
    return end;
}

void clear_out_of_order(Client* client) {
    if (client == nullptr) return;
    for (size_t index = 0U; index < OUT_OF_ORDER_QUEUE_DEPTH; ++index) {
        client->out_of_order[index].valid = false;
        client->out_of_order[index].payload_length = 0U;
    }
    client->out_of_order_count = 0U;
}

void store_buffered_segment(BufferedSegment* destination, const TcpSegment& source) {
    destination->sequence = source.sequence;
    destination->flags = source.flags;
    destination->window = source.window;
    destination->payload_length = source.payload_length;
    for (size_t index = 0U; index < source.payload_length; ++index) {
        destination->payload[index] = source.payload[index];
    }
    destination->valid = true;
}

Status queue_out_of_order(Client* client, const TcpSegment& segment) {
    if (client == nullptr || segment.payload_length > OUT_OF_ORDER_PAYLOAD_CAPACITY) {
        return Status::InvalidArgument;
    }
    if (segment.payload_length == 0U && (segment.flags & TcpFin) == 0U) {
        return Status::Ok;
    }

    const uint32_t new_end = incoming_end_sequence(segment);
    for (size_t index = 0U; index < OUT_OF_ORDER_QUEUE_DEPTH; ++index) {
        BufferedSegment& buffered = client->out_of_order[index];
        if (!buffered.valid || buffered.sequence != segment.sequence) continue;
        if (!sequence_after(new_end, buffered_end_sequence(buffered))) {
            return Status::Ok;
        }
        store_buffered_segment(&buffered, segment);
        return Status::Ok;
    }

    for (size_t index = 0U; index < OUT_OF_ORDER_QUEUE_DEPTH; ++index) {
        BufferedSegment& buffered = client->out_of_order[index];
        if (buffered.valid) continue;
        store_buffered_segment(&buffered, segment);
        ++client->out_of_order_count;
        return Status::Ok;
    }

    // The queue is intentionally bounded. If it is full, retain the segments
    // nearest RCV.NXT and discard the farthest one. The cumulative ACK below
    // leaves the missing range visible to the peer so normal TCP retransmit
    // recovery can refill it later.
    size_t farthest = 0U;
    uint32_t farthest_distance = 0U;
    for (size_t index = 0U; index < OUT_OF_ORDER_QUEUE_DEPTH; ++index) {
        const uint32_t distance =
            client->out_of_order[index].sequence - client->receive_next;
        if (index == 0U || distance > farthest_distance) {
            farthest = index;
            farthest_distance = distance;
        }
    }
    const uint32_t new_distance = segment.sequence - client->receive_next;
    if (new_distance < farthest_distance) {
        store_buffered_segment(&client->out_of_order[farthest], segment);
    }
    return Status::Ok;
}

Status ingest_contiguous(
    Client* client,
    uint32_t sequence,
    uint8_t flags,
    const uint8_t* payload,
    size_t payload_length) {
    if (client == nullptr ||
        (payload_length != 0U && payload == nullptr) ||
        payload_length > OUT_OF_ORDER_PAYLOAD_CAPACITY) {
        return Status::InvalidArgument;
    }

    const uint32_t data_end = sequence + static_cast<uint32_t>(payload_length);
    size_t skip = 0U;
    if (payload_length != 0U) {
        if (sequence_after(sequence, client->receive_next)) {
            return Status::WouldBlock;
        }
        if (sequence_before(sequence, client->receive_next)) {
            if (!sequence_after(data_end, client->receive_next)) {
                skip = payload_length;
            } else {
                skip = static_cast<size_t>(client->receive_next - sequence);
                if (skip > payload_length) return Status::MalformedPacket;
            }
        }
        const size_t remaining = payload_length - skip;
        if (remaining != 0U) {
            const Status queued = append_pending(client, payload + skip, remaining);
            if (queued != Status::Ok) return queued;
            client->receive_next += static_cast<uint32_t>(remaining);
        }
    }

    if ((flags & TcpFin) != 0U) {
        const uint32_t fin_sequence = data_end;
        if (fin_sequence == client->receive_next) {
            ++client->receive_next;
            client->peer_closed = true;
            client->state = State::CloseWait;
        }
    }
    return Status::Ok;
}

Status flush_out_of_order(Client* client) {
    if (client == nullptr) return Status::InvalidArgument;

    for (;;) {
        bool removed_duplicate = false;
        size_t candidate = OUT_OF_ORDER_QUEUE_DEPTH;

        for (size_t index = 0U; index < OUT_OF_ORDER_QUEUE_DEPTH; ++index) {
            BufferedSegment& buffered = client->out_of_order[index];
            if (!buffered.valid) continue;
            const uint32_t end = buffered_end_sequence(buffered);

            if (!sequence_after(end, client->receive_next)) {
                buffered.valid = false;
                buffered.payload_length = 0U;
                if (client->out_of_order_count != 0U) {
                    --client->out_of_order_count;
                }
                removed_duplicate = true;
                continue;
            }
            if (sequence_after(buffered.sequence, client->receive_next)) {
                continue;
            }
            if (candidate == OUT_OF_ORDER_QUEUE_DEPTH ||
                sequence_before(
                    buffered.sequence,
                    client->out_of_order[candidate].sequence)) {
                candidate = index;
            }
        }

        if (candidate == OUT_OF_ORDER_QUEUE_DEPTH) {
            if (removed_duplicate) continue;
            return Status::Ok;
        }

        BufferedSegment& buffered = client->out_of_order[candidate];
        const Status status = ingest_contiguous(
            client,
            buffered.sequence,
            buffered.flags,
            buffered.payload,
            buffered.payload_length);
        if (status == Status::QueueFull) return status;
        if (status != Status::Ok) return status;
        buffered.valid = false;
        buffered.payload_length = 0U;
        if (client->out_of_order_count != 0U) --client->out_of_order_count;

        if (client->peer_closed) {
            clear_out_of_order(client);
            return Status::Ok;
        }
    }
}

Status process_incoming(Client* client, const TcpSegment& segment) {
    if (client == nullptr || !segment_for_client(*client, segment)) {
        return Status::NotForUs;
    }

    client->peer_window = segment.window;
    if ((segment.flags & TcpRst) != 0U) {
#if !defined(KUROGANE_HOST_TEST)
        log::write_u64(log::Level::Error, "TCP", "peer RST sequence=", segment.sequence);
        log::write_u64(log::Level::Error, "TCP", "peer RST acknowledgement=", segment.acknowledgement);
        log::write_u64(log::Level::Error, "TCP", "SND.UNA=", client->send_unacknowledged);
        log::write_u64(log::Level::Error, "TCP", "SND.NXT=", client->send_next);
        log::write_u64(log::Level::Error, "TCP", "RCV.NXT=", client->receive_next);
#endif
        client->connected = false;
        client->state = State::Reset;
        return Status::InterfaceError;
    }
    record_ack(client, segment);

    if (segment.payload_length == 0U && (segment.flags & TcpFin) == 0U) {
        return Status::Ok;
    }

    const uint32_t end = incoming_end_sequence(segment);
    if (!sequence_after(end, client->receive_next)) {
        return acknowledge_or_defer(client);
    }

    if (sequence_after(segment.sequence, client->receive_next)) {
        const Status queued = queue_out_of_order(client, segment);
        if (queued != Status::Ok) return queued;
        return acknowledge_or_defer(client);
    }

    Status status = ingest_contiguous(
        client,
        segment.sequence,
        segment.flags,
        segment.payload,
        segment.payload_length);
    if (status == Status::QueueFull) {
        // Do not advance RCV.NXT when the stream buffer is full. Advertising a
        // zero window makes the peer retain/retransmit the unaccepted range.
        return acknowledge_or_defer(client);
    }
    if (status != Status::Ok) return status;

    status = flush_out_of_order(client);
    if (status != Status::Ok && status != Status::QueueFull) return status;
    return acknowledge_or_defer(client);
}

Status take_or_poll_one(Client* client, TcpSegment* out_segment) {
    if (client == nullptr || client->stack == nullptr || out_segment == nullptr) {
        return Status::InvalidArgument;
    }

    TcpSegment segment{};
    if (take_tcp_segment(client->stack, &segment) == Status::Ok) {
        if (!segment_for_client(*client, segment)) return Status::NotForUs;
        *out_segment = segment;
        return Status::Ok;
    }

    size_t processed = 0U;
    // NetworkStack currently exposes a single transport inbox. Consume one
    // frame per poll so no later frame can overwrite the TCP segment before it
    // enters this client's stream/reassembly queues.
    const Status poll_status = net::poll(client->stack, 1U, &processed);
    if (!acceptable_poll_status(poll_status)) return poll_status;
    if (poll_status == Status::InterfaceError || poll_status == Status::WouldBlock) {
        return Status::WouldBlock;
    }
    if (take_tcp_segment(client->stack, &segment) != Status::Ok) {
        return Status::WouldBlock;
    }
    if (!segment_for_client(*client, segment)) return Status::NotForUs;
    *out_segment = segment;
    return Status::Ok;
}

Status drain_pending(
    Client* client,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length) {
    if (out_length != nullptr) *out_length = 0U;
    if (client == nullptr || output == nullptr || output_capacity == 0U ||
        out_length == nullptr) {
        return Status::InvalidArgument;
    }
    if (client->pending_length == 0U) return Status::WouldBlock;
    const size_t amount = client->pending_length < output_capacity
        ? client->pending_length : output_capacity;
    for (size_t index = 0U; index < amount; ++index) {
        output[index] = client->pending[client->pending_offset + index];
    }
    client->pending_offset += amount;
    client->pending_length -= amount;
    if (client->pending_length == 0U) client->pending_offset = 0U;
    *out_length = amount;
    return Status::Ok;
}

uint64_t transmission_slice_ms(uint64_t timeout_ms) {
    const uint64_t slice = timeout_ms / kDataTransmissionLimit;
    return slice == 0U ? 1U : slice;
}

Status poll_transmit_progress(Client* client) {
    if (client == nullptr || client->stack == nullptr) return Status::InvalidArgument;
    size_t processed = 0U;
    const Status status = net::poll(client->stack, 1U, &processed);
    return acceptable_poll_status(status) ? Status::Ok : status;
}

Status queue_segment_with_backpressure(
    Client* client,
    uint32_t sequence,
    uint32_t acknowledgement,
    uint8_t flags,
    const uint8_t* payload,
    size_t payload_length,
    uint64_t timeout_ms) {
    if (client == nullptr || client->stack == nullptr || timeout_ms == 0U) {
        return Status::InvalidArgument;
    }
    const uint64_t started = drivers::pit::ticks();
    for (size_t attempt = 0U;
         window_open(started, timeout_ms, attempt);
         ++attempt) {
        const Status status = send_tcp(
            client->stack,
            client->peer,
            client->local_port,
            client->remote_port,
            sequence,
            acknowledgement,
            flags,
            advertised_window(*client),
            payload,
            payload_length);
        if (status == Status::Ok) return Status::Ok;
        if (!retryable_transmit_status(status)) return status;

        const Status poll_status = poll_transmit_progress(client);
        if (poll_status != Status::Ok) return poll_status;
        wait_for_progress();
    }
    return Status::WouldBlock;
}

} // namespace

void initialize(Client* client) {
    if (client == nullptr) return;
    *client = {};
    client->state = State::Closed;
}

Status connect(
    Client* client,
    NetworkStack* stack,
    const IPv4Address& peer,
    uint16_t local_port,
    uint16_t remote_port,
    uint32_t initial_sequence,
    uint64_t timeout_ms) {
    if (client == nullptr || stack == nullptr || !stack->initialized ||
        !stack->ipv4_configured || ipv4_is_zero(peer) || local_port == 0U ||
        remote_port == 0U || timeout_ms == 0U) {
        return Status::InvalidArgument;
    }

    initialize(client);
    client->stack = stack;
    client->peer = peer;
    client->local_port = local_port;
    client->remote_port = remote_port;
    client->send_unacknowledged = initial_sequence;
    client->send_next = initial_sequence;
    client->state = State::SynSent;

    TcpSegment stale{};
    while (take_tcp_segment(stack, &stale) == Status::Ok) {}

    const uint64_t slice_ms = transmission_slice_ms(timeout_ms);
    for (size_t transmission = 0U;
         transmission < kDataTransmissionLimit;
         ++transmission) {
        const Status send_status = queue_segment_with_backpressure(
            client,
            initial_sequence,
            0U,
            TcpSyn,
            nullptr,
            0U,
            slice_ms);
        if (send_status != Status::Ok && send_status != Status::WouldBlock) {
            client->state = State::Error;
            return send_status;
        }
        if (send_status == Status::Ok) {
            client->send_next = initial_sequence + 1U;
        }

        const uint64_t handshake_started = drivers::pit::ticks();
        for (size_t attempt = 0U;
             window_open(handshake_started, slice_ms, attempt);
             ++attempt) {
            TcpSegment segment{};
            const Status poll_status = take_or_poll_one(client, &segment);
            if (poll_status == Status::WouldBlock || poll_status == Status::NotForUs) {
                wait_for_progress();
                continue;
            }
            if (poll_status != Status::Ok) {
                client->state = State::Error;
                return poll_status;
            }
            if ((segment.flags & TcpRst) != 0U) {
#if !defined(KUROGANE_HOST_TEST)
                log::write_u64(log::Level::Error, "TCP", "peer RST during connect sequence=", segment.sequence);
                log::write_u64(log::Level::Error, "TCP", "peer RST during connect acknowledgement=", segment.acknowledgement);
#endif
                client->state = State::Reset;
                return Status::InterfaceError;
            }
            if ((segment.flags & (TcpSyn | TcpAck)) == (TcpSyn | TcpAck) &&
                segment.acknowledgement == initial_sequence + 1U) {
                client->send_next = initial_sequence + 1U;
                client->receive_next = segment.sequence + 1U;
                client->send_unacknowledged = segment.acknowledgement;
                client->peer_window = segment.window;
                client->connected = true;
                client->state = State::Established;
                const Status ack_status = acknowledge_or_defer(client);
                if (ack_status != Status::Ok) {
                    client->connected = false;
                    client->state = State::Error;
                    return ack_status;
                }
                return Status::Ok;
            }
            wait_for_progress();
        }
    }

    client->connected = false;
    client->state = State::Error;
    return Status::WouldBlock;
}

Status send(
    Client* client,
    const uint8_t* data,
    size_t length,
    uint64_t timeout_ms) {
    if (client == nullptr || !client->connected || client->stack == nullptr ||
        (length != 0U && data == nullptr) || timeout_ms == 0U) {
        return Status::InvalidArgument;
    }
    if (client->state != State::Established || client->peer_closed) {
#if !defined(KUROGANE_HOST_TEST)
        log::write_u64(
            log::Level::Error,
            "TCP",
            "send rejected state=",
            static_cast<uint64_t>(client->state));
        log::write_u64(
            log::Level::Error,
            "TCP",
            "send rejected peer_closed=",
            client->peer_closed ? 1U : 0U);
#endif
        return Status::InterfaceError;
    }
    if (length == 0U) return Status::Ok;

    size_t offset = 0U;
    while (offset < length) {
        if (client->peer_window == 0U) return Status::WouldBlock;

        const size_t remaining = length - offset;
        size_t chunk = remaining < MAX_SEGMENT_PAYLOAD
            ? remaining : MAX_SEGMENT_PAYLOAD;
        if (chunk > client->peer_window) chunk = client->peer_window;
        if (chunk == 0U) return Status::WouldBlock;

        const uint32_t sequence = client->send_next;
        const uint32_t expected_ack = sequence + static_cast<uint32_t>(chunk);
        bool acknowledged = false;
        bool sequence_committed = false;
        const uint64_t slice_ms = transmission_slice_ms(timeout_ms);

        for (size_t transmission = 0U;
             transmission < kDataTransmissionLimit && !acknowledged;
             ++transmission) {
            Status status = queue_segment_with_backpressure(
                client,
                sequence,
                client->receive_next,
                TcpPsh | TcpAck,
                data + offset,
                chunk,
                slice_ms);
            if (status == Status::WouldBlock) continue;
            if (status != Status::Ok) {
                client->connected = false;
                client->state = State::Error;
                return status;
            }
            if (!sequence_committed) {
                client->send_next = expected_ack;
                sequence_committed = true;
            }

            const uint64_t started = drivers::pit::ticks();
            for (size_t attempt = 0U;
                 window_open(started, slice_ms, attempt);
                 ++attempt) {
                TcpSegment segment{};
                status = take_or_poll_one(client, &segment);
                if (status == Status::WouldBlock || status == Status::NotForUs) {
                    wait_for_progress();
                    continue;
                }
                if (status != Status::Ok) {
                    client->connected = false;
                    client->state = State::Error;
                    return status;
                }
                status = process_incoming(client, segment);
                if (status != Status::Ok && status != Status::NotForUs) {
                    client->connected = false;
                    if (client->state != State::Reset) {
                        client->state = State::Error;
                    }
                    return status;
                }
                if (!sequence_before(client->send_unacknowledged, expected_ack)) {
                    acknowledged = true;
                    break;
                }
                wait_for_progress();
            }
        }

        if (!sequence_committed) {
            // Nothing was confirmed queued by the local NIC, so Mbed TLS may
            // safely retry the same plaintext at the unchanged SND.NXT.
            return Status::WouldBlock;
        }
        if (!acknowledged) {
#if !defined(KUROGANE_HOST_TEST)
            log::write_u64(log::Level::Warn, "TCP", "ACK deadline sequence=", sequence);
            log::write_u64(log::Level::Warn, "TCP", "ACK deadline expected=", expected_ack);
            log::write_u64(log::Level::Warn, "TCP", "ACK deadline SND.UNA=", client->send_unacknowledged);
            log::write_u64(log::Level::Warn, "TCP", "ACK deadline SND.NXT=", client->send_next);
            log::write_u64(log::Level::Warn, "TCP", "ACK deadline RCV.NXT=", client->receive_next);
            log::write_u64(log::Level::Warn, "TCP", "ACK deadline peer window=", client->peer_window);
#endif
            // No byte from this TCP segment was acknowledged. Rewind SND.NXT
            // to the original sequence and report WouldBlock so a nonblocking
            // caller (Mbed TLS BIO) can retry the identical plaintext at the
            // identical TCP sequence. This is safe even if the original wire
            // packet arrived but its ACK was lost: TCP duplicate suppression
            // prevents duplicate application bytes at the peer.
            if (client->send_unacknowledged == sequence) {
                client->send_next = sequence;
                return Status::WouldBlock;
            }

            // A partial ACK means some bytes are already committed in the peer
            // stream. Rewinding the whole application buffer would duplicate
            // data, so keep this rare case fatal until residual-byte tracking
            // is implemented explicitly.
            client->connected = false;
            client->state = State::Error;
            return Status::InterfaceError;
        }
        offset += chunk;
    }
    return Status::Ok;
}

Status receive(
    Client* client,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_length,
    uint64_t timeout_ms) {
    if (out_length != nullptr) *out_length = 0U;
    if (client == nullptr || client->stack == nullptr || output == nullptr ||
        output_capacity == 0U || out_length == nullptr || timeout_ms == 0U) {
        return Status::InvalidArgument;
    }
    if (client->state == State::Reset || client->state == State::Error) {
        return Status::InterfaceError;
    }

    Status status = drain_pending(client, output, output_capacity, out_length);
    if (status == Status::Ok) {
        static_cast<void>(acknowledge_or_defer(client));
        return status;
    }
    if (client->peer_closed) return Status::Ok;
    if (!client->connected) return Status::InterfaceError;

    const uint64_t started = drivers::pit::ticks();
    for (size_t attempt = 0U;
         window_open(started, timeout_ms, attempt);
         ++attempt) {
        TcpSegment segment{};
        status = take_or_poll_one(client, &segment);
        if (status == Status::WouldBlock || status == Status::NotForUs) {
            wait_for_progress();
            continue;
        }
        if (status != Status::Ok) return status;

        status = process_incoming(client, segment);
        if (status != Status::Ok && status != Status::NotForUs) return status;

        status = drain_pending(client, output, output_capacity, out_length);
        if (status == Status::Ok) {
            static_cast<void>(acknowledge_or_defer(client));
            return status;
        }
        if (client->peer_closed) return Status::Ok;
        wait_for_progress();
    }
    return Status::WouldBlock;
}

Status close(Client* client) {
    if (client == nullptr) return Status::InvalidArgument;
    if (!client->connected || client->stack == nullptr) {
        initialize(client);
        return Status::Ok;
    }

    client->state = State::FinWait1;
    const Status status = send_tcp(
        client->stack,
        client->peer,
        client->local_port,
        client->remote_port,
        client->send_next,
        client->receive_next,
        TcpFin | TcpAck,
        advertised_window(*client),
        nullptr,
        0U);
    if (status == Status::Ok) ++client->send_next;
    initialize(client);
    return retryable_transmit_status(status) ? Status::Ok : status;
}

} // namespace net::tcp_client

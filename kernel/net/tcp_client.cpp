#include "tcp_client.hpp"

#include "../arch/x86_64/io.hpp"
#include "../drivers/pit.hpp"

namespace net::tcp_client {
namespace {

constexpr uint16_t kAdvertisedWindow = UINT16_C(32768);
constexpr size_t kFallbackPollBudget = 800000U;

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

bool segment_for_client(const Client& client, const TcpSegment& segment) {
    return segment.valid && ipv4_equal(segment.source, client.peer) &&
        segment.source_port == client.remote_port &&
        segment.destination_port == client.local_port;
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
            PENDING_RECEIVE_CAPACITY) {
        compact_pending(client);
    }
    const size_t end = client->pending_offset + client->pending_length;
    if (end > PENDING_RECEIVE_CAPACITY ||
        length > PENDING_RECEIVE_CAPACITY - end) {
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
        kAdvertisedWindow,
        nullptr,
        0U);
}

Status process_incoming(Client* client, const TcpSegment& segment) {
    if (client == nullptr || !segment_for_client(*client, segment)) {
        return Status::NotForUs;
    }
    if ((segment.flags & TcpRst) != 0U) {
        client->connected = false;
        return Status::InterfaceError;
    }

    if (segment.payload_length != 0U) {
        if (segment.sequence < client->receive_next) {
            return acknowledge(client);
        }
        if (segment.sequence != client->receive_next) {
            return Status::WouldBlock;
        }
        const Status queued = append_pending(
            client, segment.payload, segment.payload_length);
        if (queued != Status::Ok) return queued;
        client->receive_next += static_cast<uint32_t>(segment.payload_length);
    }

    if ((segment.flags & TcpFin) != 0U) {
        if (segment.sequence + static_cast<uint32_t>(segment.payload_length) !=
            client->receive_next) {
            return Status::WouldBlock;
        }
        ++client->receive_next;
        client->peer_closed = true;
    }

    if (segment.payload_length != 0U || (segment.flags & TcpFin) != 0U) {
        const Status ack_status = acknowledge(client);
        if (ack_status != Status::Ok &&
            ack_status != Status::NeighborResolutionPending) {
            return ack_status;
        }
    }
    return Status::Ok;
}

Status poll_one(Client* client, TcpSegment* out_segment) {
    if (client == nullptr || client->stack == nullptr) {
        return Status::InvalidArgument;
    }
    size_t processed = 0U;
    const Status poll_status = net::poll(client->stack, 4U, &processed);
    if (poll_status != Status::Ok && poll_status != Status::NotForUs &&
        poll_status != Status::UnsupportedProtocol) {
        return poll_status;
    }
    TcpSegment segment{};
    const Status take_status = take_tcp_segment(client->stack, &segment);
    if (take_status != Status::Ok) return Status::WouldBlock;
    if (!segment_for_client(*client, segment)) return Status::NotForUs;
    if (out_segment != nullptr) *out_segment = segment;
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

} // namespace

void initialize(Client* client) {
    if (client == nullptr) return;
    *client = {};
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
    client->send_next = initial_sequence;

    TcpSegment stale{};
    while (take_tcp_segment(stack, &stale) == Status::Ok) {}

    bool syn_sent = false;
    const uint64_t send_started = drivers::pit::ticks();
    for (size_t attempt = 0U;
         window_open(send_started, timeout_ms, attempt);
         ++attempt) {
        const Status status = send_tcp(
            stack, peer, local_port, remote_port,
            initial_sequence, 0U, TcpSyn, kAdvertisedWindow, nullptr, 0U);
        if (status == Status::Ok) {
            syn_sent = true;
            break;
        }
        if (status != Status::NeighborResolutionPending) return status;
        size_t processed = 0U;
        const Status poll_status = net::poll(stack, 4U, &processed);
        if (poll_status != Status::Ok && poll_status != Status::NotForUs &&
            poll_status != Status::UnsupportedProtocol) {
            return poll_status;
        }
        wait_for_progress();
    }
    if (!syn_sent) return Status::WouldBlock;

    const uint64_t handshake_started = drivers::pit::ticks();
    for (size_t attempt = 0U;
         window_open(handshake_started, timeout_ms, attempt);
         ++attempt) {
        size_t processed = 0U;
        const Status poll_status = net::poll(stack, 4U, &processed);
        if (poll_status != Status::Ok && poll_status != Status::NotForUs &&
            poll_status != Status::UnsupportedProtocol) {
            return poll_status;
        }
        TcpSegment segment{};
        if (take_tcp_segment(stack, &segment) == Status::Ok &&
            segment_for_client(*client, segment)) {
            if ((segment.flags & TcpRst) != 0U) return Status::InterfaceError;
            if ((segment.flags & (TcpSyn | TcpAck)) == (TcpSyn | TcpAck) &&
                segment.acknowledgement == initial_sequence + 1U) {
                client->send_next = initial_sequence + 1U;
                client->receive_next = segment.sequence + 1U;
                client->connected = true;
                const Status ack_status = acknowledge(client);
                if (ack_status != Status::Ok) {
                    client->connected = false;
                    return ack_status;
                }
                return Status::Ok;
            }
        }
        wait_for_progress();
    }
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
    if (client->peer_closed) return Status::InterfaceError;
    if (length == 0U) return Status::Ok;

    size_t offset = 0U;
    while (offset < length) {
        const size_t remaining = length - offset;
        const size_t chunk = remaining < MAX_SEGMENT_PAYLOAD
            ? remaining : MAX_SEGMENT_PAYLOAD;
        const uint32_t sequence = client->send_next;
        const uint32_t expected_ack = sequence + static_cast<uint32_t>(chunk);
        Status status = send_tcp(
            client->stack,
            client->peer,
            client->local_port,
            client->remote_port,
            sequence,
            client->receive_next,
            TcpPsh | TcpAck,
            kAdvertisedWindow,
            data + offset,
            chunk);
        if (status != Status::Ok) return status;
        client->send_next = expected_ack;

        bool acknowledged = false;
        const uint64_t started = drivers::pit::ticks();
        for (size_t attempt = 0U; window_open(started, timeout_ms, attempt); ++attempt) {
            TcpSegment segment{};
            status = poll_one(client, &segment);
            if (status == Status::WouldBlock || status == Status::NotForUs) {
                wait_for_progress();
                continue;
            }
            if (status != Status::Ok) return status;
            const bool acked = (segment.flags & TcpAck) != 0U &&
                segment.acknowledgement >= expected_ack;
            status = process_incoming(client, segment);
            if (status != Status::Ok && status != Status::NotForUs) return status;
            if (acked) {
                acknowledged = true;
                break;
            }
            wait_for_progress();
        }
        if (!acknowledged) return Status::WouldBlock;
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

    Status status = drain_pending(client, output, output_capacity, out_length);
    if (status == Status::Ok) return status;
    if (client->peer_closed) return Status::Ok;
    if (!client->connected) return Status::InterfaceError;

    const uint64_t started = drivers::pit::ticks();
    for (size_t attempt = 0U; window_open(started, timeout_ms, attempt); ++attempt) {
        TcpSegment segment{};
        status = poll_one(client, &segment);
        if (status == Status::WouldBlock || status == Status::NotForUs) {
            wait_for_progress();
            continue;
        }
        if (status != Status::Ok) return status;
        status = process_incoming(client, segment);
        if (status != Status::Ok && status != Status::NotForUs) return status;
        status = drain_pending(client, output, output_capacity, out_length);
        if (status == Status::Ok) return status;
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
    const Status status = send_tcp(
        client->stack,
        client->peer,
        client->local_port,
        client->remote_port,
        client->send_next,
        client->receive_next,
        TcpFin | TcpAck,
        kAdvertisedWindow,
        nullptr,
        0U);
    initialize(client);
    return status == Status::NeighborResolutionPending ? Status::Ok : status;
}

} // namespace net::tcp_client

#include "socket.hpp"

namespace net::socket {
namespace {

struct QueuedDatagram {
    Endpoint source;
    uint8_t payload[UDP_MAX_PAYLOAD];
    size_t size;
};

struct Slot {
    ProcessId owner;
    uint32_t generation;
    Type type;
    Protocol protocol;
    Endpoint local;
    Endpoint remote;
    QueuedDatagram rx[MAX_RX_DATAGRAMS];
    size_t rx_head;
    size_t rx_tail;
    size_t rx_count;
    size_t tcp_session;
    bool active;
    bool bound;
    bool connected;
};

struct TcpSession {
    tcp_client::Client client;
    bool active;
};

Backend g_backend{};
Slot g_slots[MAX_SOCKETS]{};
TcpSession g_tcp_sessions[MAX_TCP_SESSIONS]{};
bool g_initialized = false;
uint16_t g_next_ephemeral = EPHEMERAL_PORT_FIRST;
uint32_t g_next_tcp_sequence = UINT32_C(0x4b550001);

bool address_equal(const IPv4Address& left, const IPv4Address& right) {
    for (size_t index = 0U; index < IPV4_ADDRESS_LENGTH; ++index) {
        if (left.bytes[index] != right.bytes[index]) return false;
    }
    return true;
}

bool address_zero(const IPv4Address& address) {
    for (size_t index = 0U; index < IPV4_ADDRESS_LENGTH; ++index) {
        if (address.bytes[index] != 0U) return false;
    }
    return true;
}

bool address_loopback(const IPv4Address& address) {
    return address.bytes[0] == 127U;
}

Handle encode_handle(size_t index, uint32_t generation) {
    return (static_cast<uint64_t>(generation) << 32U) |
        static_cast<uint64_t>(index + 1U);
}

bool decode_index(Handle handle, size_t* index, uint32_t* generation) {
    if (handle == INVALID_HANDLE || index == nullptr || generation == nullptr) {
        return false;
    }
    const uint64_t encoded = handle & UINT64_C(0xFFFFFFFF);
    if (encoded == 0U || encoded > MAX_SOCKETS) return false;
    *index = static_cast<size_t>(encoded - 1U);
    *generation = static_cast<uint32_t>(handle >> 32U);
    return *generation != 0U;
}

Slot* resolve(ProcessId owner, Handle handle, Status* failure) {
    size_t index = 0U;
    uint32_t generation = 0U;
    if (!decode_index(handle, &index, &generation)) {
        if (failure != nullptr) *failure = Status::StaleHandle;
        return nullptr;
    }
    Slot& slot = g_slots[index];
    if (!slot.active || slot.generation != generation) {
        if (failure != nullptr) *failure = Status::StaleHandle;
        return nullptr;
    }
    if (slot.owner != owner) {
        if (failure != nullptr) *failure = Status::AccessDenied;
        return nullptr;
    }
    if (failure != nullptr) *failure = Status::Ok;
    return &slot;
}

void release_tcp_session(Slot& slot) {
    if (slot.tcp_session >= MAX_TCP_SESSIONS) return;
    TcpSession& session = g_tcp_sessions[slot.tcp_session];
    tcp_client::initialize(&session.client);
    session = {};
    slot.tcp_session = MAX_TCP_SESSIONS;
}

void clear_slot(Slot& slot) {
    release_tcp_session(slot);
    const uint32_t generation = slot.generation;
    slot = {};
    slot.generation = generation;
    slot.tcp_session = MAX_TCP_SESSIONS;
}

bool endpoints_overlap(const Endpoint& left, const Endpoint& right) {
    if (left.port != right.port) return false;
    return address_zero(left.address) || address_zero(right.address) ||
        address_equal(left.address, right.address);
}

bool endpoint_in_use(
    const Endpoint& endpoint,
    Protocol protocol,
    const Slot* ignored = nullptr) {
    for (const Slot& slot : g_slots) {
        if (!slot.active || !slot.bound || &slot == ignored ||
            slot.protocol != protocol) {
            continue;
        }
        if (endpoints_overlap(slot.local, endpoint)) return true;
    }
    return false;
}

uint16_t allocate_ephemeral_port(const Slot* ignored) {
    if (ignored == nullptr) return 0U;
    constexpr uint32_t range = UINT32_C(65536) - EPHEMERAL_PORT_FIRST;
    for (uint32_t attempt = 0U; attempt < range; ++attempt) {
        const uint16_t candidate = g_next_ephemeral;
        ++g_next_ephemeral;
        if (g_next_ephemeral < EPHEMERAL_PORT_FIRST) {
            g_next_ephemeral = EPHEMERAL_PORT_FIRST;
        }
        Endpoint endpoint{{{0U, 0U, 0U, 0U}}, candidate};
        if (!endpoint_in_use(endpoint, ignored->protocol, ignored)) return candidate;
    }
    return 0U;
}

uint32_t next_tcp_initial_sequence() {
    const uint32_t result = g_next_tcp_sequence;
    g_next_tcp_sequence += UINT32_C(0x00010001);
    if (g_next_tcp_sequence == 0U) g_next_tcp_sequence = UINT32_C(0x4b550001);
    return result;
}

TcpSession* reserve_tcp_session(size_t* out_index) {
    if (out_index != nullptr) *out_index = MAX_TCP_SESSIONS;
    for (size_t index = 0U; index < MAX_TCP_SESSIONS; ++index) {
        TcpSession& session = g_tcp_sessions[index];
        if (session.active) continue;
        session = {};
        session.active = true;
        tcp_client::initialize(&session.client);
        if (out_index != nullptr) *out_index = index;
        return &session;
    }
    return nullptr;
}

Status transport_status(net::Status status) {
    switch (status) {
        case net::Status::Ok:
        case net::Status::NotForUs:
        case net::Status::UnsupportedProtocol:
            return Status::Ok;
        case net::Status::WouldBlock:
        case net::Status::QueueFull:
        case net::Status::NeighborResolutionPending:
            return Status::WouldBlock;
        case net::Status::BufferTooSmall:
            return Status::BufferTooSmall;
        case net::Status::PayloadTooLarge:
            return Status::PayloadTooLarge;
        case net::Status::NotInitialized:
        case net::Status::NotConfigured:
            return Status::NotInitialized;
        case net::Status::InvalidArgument:
        case net::Status::InvalidConfiguration:
            return Status::InvalidArgument;
        default:
            return Status::TransportError;
    }
}

bool slot_accepts_datagram(
    const Slot& slot,
    const Endpoint& source,
    const Endpoint& destination) {
    if (!slot.active || !slot.bound || slot.protocol != Protocol::Udp ||
        slot.local.port != destination.port) {
        return false;
    }
    if (!address_zero(slot.local.address) &&
        !address_equal(slot.local.address, destination.address)) {
        return false;
    }
    return !slot.connected ||
        (slot.remote.port == source.port &&
         address_equal(slot.remote.address, source.address));
}

bool datagram_matches(const Slot& slot, const UdpDatagram& datagram) {
    const Endpoint source{datagram.source, datagram.source_port};
    const Endpoint destination{datagram.destination, datagram.destination_port};
    return slot_accepts_datagram(slot, source, destination);
}

bool enqueue_payload(
    Slot& slot,
    const Endpoint& source,
    const uint8_t* payload,
    size_t payload_length) {
    if (slot.rx_count >= MAX_RX_DATAGRAMS || payload == nullptr ||
        payload_length == 0U || payload_length > UDP_MAX_PAYLOAD) {
        return false;
    }
    QueuedDatagram& queued = slot.rx[slot.rx_head];
    queued = {};
    queued.source = source;
    queued.size = payload_length;
    for (size_t index = 0U; index < queued.size; ++index) {
        queued.payload[index] = payload[index];
    }
    slot.rx_head = (slot.rx_head + 1U) % MAX_RX_DATAGRAMS;
    ++slot.rx_count;
    return true;
}

bool enqueue(Slot& slot, const UdpDatagram& datagram) {
    return enqueue_payload(
        slot,
        {datagram.source, datagram.source_port},
        datagram.payload,
        datagram.payload_length);
}

Status send_loopback(const Slot& sender, const uint8_t* payload, size_t size) {
    static constexpr IPv4Address canonical_loopback{{127U, 0U, 0U, 1U}};
    const Endpoint source{
        address_zero(sender.local.address) ? canonical_loopback : sender.local.address,
        sender.local.port};
    const Endpoint destination{sender.remote.address, sender.remote.port};

    for (Slot& candidate : g_slots) {
        if (!slot_accepts_datagram(candidate, source, destination)) continue;
        return enqueue_payload(candidate, source, payload, size)
            ? Status::Ok : Status::WouldBlock;
    }

    // UDP send to a local but currently unbound port succeeds synchronously;
    // the datagram is simply not queued to any socket. Never leak 127/8 to NIC.
    return Status::Ok;
}

} // namespace

Status initialize(const Backend& backend) {
    if (g_initialized) return Status::AlreadyInitialized;
    if (backend.send_udp == nullptr || backend.poll == nullptr ||
        backend.take_udp == nullptr || backend.tcp_begin_connect == nullptr ||
        backend.tcp_progress == nullptr || backend.tcp_try_send == nullptr ||
        backend.tcp_try_receive == nullptr || backend.tcp_begin_close == nullptr) {
        return Status::InvalidArgument;
    }
    for (Slot& slot : g_slots) {
        slot = {};
        slot.tcp_session = MAX_TCP_SESSIONS;
    }
    for (TcpSession& session : g_tcp_sessions) session = {};
    g_backend = backend;
    g_next_ephemeral = EPHEMERAL_PORT_FIRST;
    g_next_tcp_sequence = UINT32_C(0x4b550001);
    g_initialized = true;
    return Status::Ok;
}

bool initialized() { return g_initialized; }

Status create(
    ProcessId owner,
    Type type,
    Protocol protocol,
    Handle* output) {
    if (output != nullptr) *output = INVALID_HANDLE;
    if (!g_initialized) return Status::NotInitialized;
    if (owner == 0U || output == nullptr) return Status::InvalidArgument;
    const bool udp = type == Type::Datagram && protocol == Protocol::Udp;
    const bool tcp = type == Type::Stream && protocol == Protocol::Tcp;
    if (!udp && !tcp) return Status::NotSupported;
    for (size_t index = 0U; index < MAX_SOCKETS; ++index) {
        Slot& slot = g_slots[index];
        if (slot.active) continue;
        ++slot.generation;
        if (slot.generation == 0U) slot.generation = 1U;
        const uint32_t generation = slot.generation;
        slot = {};
        slot.generation = generation;
        slot.owner = owner;
        slot.type = type;
        slot.protocol = protocol;
        slot.tcp_session = MAX_TCP_SESSIONS;
        slot.active = true;
        *output = encode_handle(index, generation);
        return Status::Ok;
    }
    return Status::CapacityReached;
}

Status bind(ProcessId owner, Handle handle, const Endpoint& endpoint) {
    if (!g_initialized) return Status::NotInitialized;
    if (endpoint.port == 0U) return Status::InvalidArgument;
    Status failure = Status::Ok;
    Slot* slot = resolve(owner, handle, &failure);
    if (slot == nullptr) return failure;
    if (endpoint_in_use(endpoint, slot->protocol, slot)) return Status::AddressInUse;
    slot->local = endpoint;
    slot->bound = true;
    return Status::Ok;
}

Status connect(ProcessId owner, Handle handle, const Endpoint& endpoint) {
    if (!g_initialized) return Status::NotInitialized;
    if (endpoint.port == 0U || address_zero(endpoint.address)) {
        return Status::InvalidArgument;
    }
    Status failure = Status::Ok;
    Slot* slot = resolve(owner, handle, &failure);
    if (slot == nullptr) return failure;
    if (!slot->bound) {
        const uint16_t port = allocate_ephemeral_port(slot);
        if (port == 0U) return Status::CapacityReached;
        slot->local = {{{0U, 0U, 0U, 0U}}, port};
        slot->bound = true;
    }

    if (slot->protocol == Protocol::Udp && slot->type == Type::Datagram) {
        slot->remote = endpoint;
        slot->connected = true;
        return Status::Ok;
    }
    if (slot->protocol != Protocol::Tcp || slot->type != Type::Stream) {
        return Status::NotSupported;
    }

    if (slot->tcp_session < MAX_TCP_SESSIONS) {
        if (!address_equal(slot->remote.address, endpoint.address) ||
            slot->remote.port != endpoint.port) {
            return Status::InvalidArgument;
        }
        TcpSession& session = g_tcp_sessions[slot->tcp_session];
        const net::Status progress_status =
            g_backend.tcp_progress(g_backend.context, &session.client);
        slot->connected = session.client.connected;
        if (slot->connected) return Status::Ok;
        return transport_status(progress_status);
    }

    size_t session_index = MAX_TCP_SESSIONS;
    TcpSession* session = reserve_tcp_session(&session_index);
    if (session == nullptr) return Status::CapacityReached;
    slot->tcp_session = session_index;
    slot->remote = endpoint;
    const net::Status begin_status = g_backend.tcp_begin_connect(
        g_backend.context,
        &session->client,
        endpoint.address,
        slot->local.port,
        endpoint.port,
        next_tcp_initial_sequence());
    slot->connected = session->client.connected;
    const Status mapped = transport_status(begin_status);
    if (mapped != Status::Ok && mapped != Status::WouldBlock) {
        release_tcp_session(*slot);
        return mapped;
    }
    return slot->connected ? Status::Ok : Status::WouldBlock;
}

Status send(
    ProcessId owner,
    Handle handle,
    const void* data,
    size_t size,
    size_t* out_sent) {
    if (out_sent != nullptr) *out_sent = 0U;
    if (!g_initialized) return Status::NotInitialized;
    if (data == nullptr || size == 0U) return Status::InvalidArgument;
    Status failure = Status::Ok;
    Slot* slot = resolve(owner, handle, &failure);
    if (slot == nullptr) return failure;
    if (!slot->bound) return Status::NotBound;

    if (slot->protocol == Protocol::Udp && slot->type == Type::Datagram) {
        if (size > UDP_MAX_PAYLOAD) return Status::PayloadTooLarge;
        if (!slot->connected) return Status::NotConnected;
        Status status = Status::Ok;
        if (address_loopback(slot->remote.address)) {
            status = send_loopback(*slot, static_cast<const uint8_t*>(data), size);
        } else {
            status = transport_status(g_backend.send_udp(
                g_backend.context,
                slot->remote.address,
                slot->local.port,
                slot->remote.port,
                static_cast<const uint8_t*>(data),
                size));
        }
        if (status == Status::Ok && out_sent != nullptr) *out_sent = size;
        return status;
    }

    if (slot->protocol != Protocol::Tcp || slot->type != Type::Stream ||
        slot->tcp_session >= MAX_TCP_SESSIONS) {
        return Status::NotConnected;
    }
    TcpSession& session = g_tcp_sessions[slot->tcp_session];
    if (!slot->connected) {
        const net::Status progress_status =
            g_backend.tcp_progress(g_backend.context, &session.client);
        slot->connected = session.client.connected;
        if (!slot->connected) {
            const Status mapped = transport_status(progress_status);
            return mapped == Status::Ok ? Status::WouldBlock : mapped;
        }
    }
    size_t sent = 0U;
    const Status status = transport_status(g_backend.tcp_try_send(
        g_backend.context,
        &session.client,
        static_cast<const uint8_t*>(data),
        size,
        &sent));
    if (status == Status::Ok && out_sent != nullptr) *out_sent = sent;
    return status;
}

Status pump(size_t budget, size_t* out_routed) {
    if (out_routed != nullptr) *out_routed = 0U;
    if (!g_initialized) return Status::NotInitialized;
    if (budget == 0U) return Status::Ok;
    size_t routed = 0U;
    bool queue_full = false;
    for (size_t attempt = 0U; attempt < budget; ++attempt) {
        size_t processed = 0U;
        const Status poll_status = transport_status(
            g_backend.poll(g_backend.context, 1U, &processed));
        if (poll_status != Status::Ok && poll_status != Status::WouldBlock) {
            if (out_routed != nullptr) *out_routed = routed;
            return poll_status;
        }
        UdpDatagram datagram{};
        const net::Status take_status =
            g_backend.take_udp(g_backend.context, &datagram);
        if (take_status == net::Status::Ok) {
            bool delivered = false;
            for (Slot& slot : g_slots) {
                if (!datagram_matches(slot, datagram)) continue;
                delivered = true;
                if (enqueue(slot, datagram)) {
                    ++routed;
                } else {
                    queue_full = true;
                }
                break;
            }
            static_cast<void>(delivered);
            continue;
        }
        if (take_status != net::Status::WouldBlock &&
            take_status != net::Status::NotForUs) {
            if (out_routed != nullptr) *out_routed = routed;
            return transport_status(take_status);
        }
        if (processed == 0U) break;
    }
    if (out_routed != nullptr) *out_routed = routed;
    return queue_full ? Status::WouldBlock : Status::Ok;
}

Status receive(
    ProcessId owner,
    Handle handle,
    void* output,
    size_t capacity,
    size_t* out_size,
    Endpoint* out_source) {
    if (out_size != nullptr) *out_size = 0U;
    if (!g_initialized) return Status::NotInitialized;
    if (output == nullptr || capacity == 0U || out_size == nullptr) {
        return Status::InvalidArgument;
    }
    Status failure = Status::Ok;
    Slot* slot = resolve(owner, handle, &failure);
    if (slot == nullptr) return failure;
    if (!slot->bound) return Status::NotBound;

    if (slot->protocol == Protocol::Tcp && slot->type == Type::Stream) {
        if (slot->tcp_session >= MAX_TCP_SESSIONS) return Status::NotConnected;
        TcpSession& session = g_tcp_sessions[slot->tcp_session];
        if (session.client.state == tcp_client::State::Closed) {
            if (out_source != nullptr) *out_source = slot->remote;
            return Status::Ok;
        }
        if (!slot->connected) {
            const net::Status progress_status =
                g_backend.tcp_progress(g_backend.context, &session.client);
            slot->connected = session.client.connected;
            if (!slot->connected) {
                const Status mapped = transport_status(progress_status);
                return mapped == Status::Ok ? Status::WouldBlock : mapped;
            }
        }
        const Status status = transport_status(g_backend.tcp_try_receive(
            g_backend.context,
            &session.client,
            static_cast<uint8_t*>(output),
            capacity,
            out_size));
        slot->connected = session.client.connected;
        if (out_source != nullptr) *out_source = slot->remote;
        return status;
    }

    if (slot->protocol != Protocol::Udp || slot->type != Type::Datagram) {
        return Status::NotSupported;
    }
    if (slot->rx_count == 0U) {
        const Status pump_status = pump(8U, nullptr);
        if (pump_status != Status::Ok && pump_status != Status::WouldBlock) {
            return pump_status;
        }
    }
    if (slot->rx_count == 0U) return Status::WouldBlock;
    QueuedDatagram& queued = slot->rx[slot->rx_tail];
    *out_size = queued.size;
    if (capacity < queued.size) return Status::BufferTooSmall;
    auto* bytes = static_cast<uint8_t*>(output);
    for (size_t index = 0U; index < queued.size; ++index) {
        bytes[index] = queued.payload[index];
    }
    if (out_source != nullptr) *out_source = queued.source;
    queued = {};
    slot->rx_tail = (slot->rx_tail + 1U) % MAX_RX_DATAGRAMS;
    --slot->rx_count;
    return Status::Ok;
}

Status close(ProcessId owner, Handle handle) {
    if (!g_initialized) return Status::NotInitialized;
    Status failure = Status::Ok;
    Slot* slot = resolve(owner, handle, &failure);
    if (slot == nullptr) return failure;
    if (slot->protocol != Protocol::Tcp || slot->type != Type::Stream ||
        slot->tcp_session >= MAX_TCP_SESSIONS) {
        clear_slot(*slot);
        return Status::Ok;
    }

    TcpSession& session = g_tcp_sessions[slot->tcp_session];
    if (session.client.state == tcp_client::State::Closed) {
        clear_slot(*slot);
        return Status::Ok;
    }
    const Status begin_status = transport_status(
        g_backend.tcp_begin_close(g_backend.context, &session.client));
    if (begin_status != Status::Ok && begin_status != Status::WouldBlock) {
        return begin_status;
    }
    if (session.client.state != tcp_client::State::Closed) {
        const Status progress_status = transport_status(
            g_backend.tcp_progress(g_backend.context, &session.client));
        if (progress_status != Status::Ok && progress_status != Status::WouldBlock) {
            return progress_status;
        }
    }
    if (session.client.state != tcp_client::State::Closed) return Status::WouldBlock;
    clear_slot(*slot);
    return Status::Ok;
}


Status readiness(
    ProcessId owner,
    Handle handle,
    uint32_t requested,
    uint32_t* out_ready) {
    if (out_ready != nullptr) *out_ready = ReadyNone;
    if (!g_initialized) return Status::NotInitialized;
    if (out_ready == nullptr || requested == 0U ||
        (requested & ~static_cast<uint32_t>(ReadyAll)) != 0U) {
        return Status::InvalidArgument;
    }
    Status failure = Status::Ok;
    Slot* slot = resolve(owner, handle, &failure);
    if (slot == nullptr) return failure;

    uint32_t ready = ReadyNone;
    if (slot->protocol == Protocol::Udp && slot->type == Type::Datagram) {
        if ((requested & ReadyRead) != 0U && slot->rx_count == 0U) {
            const Status pump_status = pump(8U, nullptr);
            if (pump_status != Status::Ok && pump_status != Status::WouldBlock) {
                return pump_status;
            }
        }
        if (slot->rx_count != 0U) ready |= ReadyRead;
        if (slot->bound && slot->connected) {
            ready |= ReadyWrite;
            ready |= ReadyConnected;
        }
        *out_ready = ready & requested;
        return Status::Ok;
    }

    if (slot->protocol != Protocol::Tcp || slot->type != Type::Stream) {
        return Status::NotSupported;
    }
    if (slot->tcp_session >= MAX_TCP_SESSIONS) {
        *out_ready = ReadyNone;
        return Status::Ok;
    }

    TcpSession& session = g_tcp_sessions[slot->tcp_session];
    const net::Status progress_status =
        g_backend.tcp_progress(g_backend.context, &session.client);
    slot->connected = session.client.connected;
    if (slot->connected) {
        ready |= ReadyConnected;
        if (session.client.state == tcp_client::State::Established) ready |= ReadyWrite;
    }
    if (session.client.pending_length != 0U || session.client.peer_closed) {
        ready |= ReadyRead;
    }
    if (session.client.peer_closed ||
        session.client.state == tcp_client::State::CloseWait ||
        session.client.state == tcp_client::State::Closed) {
        ready |= ReadyHangup;
    }
    if (session.client.state == tcp_client::State::Reset ||
        session.client.state == tcp_client::State::Error) {
        ready |= ReadyError;
    }
    const Status progress = transport_status(progress_status);
    if (progress != Status::Ok && progress != Status::WouldBlock &&
        (ready & ReadyError) == 0U) {
        return progress;
    }
    *out_ready = ready & requested;
    return Status::Ok;
}

void release_process(ProcessId owner) {
    if (!g_initialized || owner == 0U) return;
    for (Slot& slot : g_slots) {
        if (slot.active && slot.owner == owner) clear_slot(slot);
    }
}

size_t active_count(ProcessId owner) {
    if (!g_initialized) return 0U;
    size_t count = 0U;
    for (const Slot& slot : g_slots) {
        if (slot.active && (owner == 0U || slot.owner == owner)) ++count;
    }
    return count;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::AlreadyInitialized: return "already initialized";
        case Status::NotInitialized: return "not initialized";
        case Status::InvalidArgument: return "invalid argument";
        case Status::NotSupported: return "socket type/protocol not supported";
        case Status::CapacityReached: return "socket capacity reached";
        case Status::AccessDenied: return "socket owned by another process";
        case Status::StaleHandle: return "stale socket handle";
        case Status::AddressInUse: return "socket address already in use";
        case Status::NotBound: return "socket is not bound";
        case Status::NotConnected: return "socket is not connected";
        case Status::WouldBlock: return "socket operation would block";
        case Status::BufferTooSmall: return "socket receive buffer too small";
        case Status::PayloadTooLarge: return "socket payload too large";
        case Status::TransportError: return "network transport error";
    }
    return "unknown socket status";
}

} // namespace net::socket

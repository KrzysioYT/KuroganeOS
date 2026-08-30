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
    bool active;
    bool bound;
    bool connected;
};

Backend g_backend{};
Slot g_slots[MAX_SOCKETS]{};
bool g_initialized = false;
uint16_t g_next_ephemeral = EPHEMERAL_PORT_FIRST;

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

void clear_slot(Slot& slot) {
    const uint32_t generation = slot.generation;
    slot = {};
    slot.generation = generation;
}

bool endpoints_overlap(const Endpoint& left, const Endpoint& right) {
    if (left.port != right.port) return false;
    return address_zero(left.address) || address_zero(right.address) ||
        address_equal(left.address, right.address);
}

bool endpoint_in_use(const Endpoint& endpoint, const Slot* ignored = nullptr) {
    for (const Slot& slot : g_slots) {
        if (!slot.active || !slot.bound || &slot == ignored) continue;
        if (endpoints_overlap(slot.local, endpoint)) return true;
    }
    return false;
}

uint16_t allocate_ephemeral_port(const Slot* ignored) {
    constexpr uint32_t range = UINT32_C(65536) - EPHEMERAL_PORT_FIRST;
    for (uint32_t attempt = 0U; attempt < range; ++attempt) {
        const uint16_t candidate = g_next_ephemeral;
        ++g_next_ephemeral;
        if (g_next_ephemeral < EPHEMERAL_PORT_FIRST) {
            g_next_ephemeral = EPHEMERAL_PORT_FIRST;
        }
        Endpoint endpoint{{{0U, 0U, 0U, 0U}}, candidate};
        if (!endpoint_in_use(endpoint, ignored)) return candidate;
    }
    return 0U;
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

bool datagram_matches(const Slot& slot, const UdpDatagram& datagram) {
    if (!slot.active || !slot.bound || slot.protocol != Protocol::Udp ||
        slot.local.port != datagram.destination_port) {
        return false;
    }
    if (!address_zero(slot.local.address) &&
        !address_equal(slot.local.address, datagram.destination)) {
        return false;
    }
    return !slot.connected ||
        (slot.remote.port == datagram.source_port &&
         address_equal(slot.remote.address, datagram.source));
}

bool enqueue(Slot& slot, const UdpDatagram& datagram) {
    if (slot.rx_count >= MAX_RX_DATAGRAMS ||
        datagram.payload_length > UDP_MAX_PAYLOAD) {
        return false;
    }
    QueuedDatagram& queued = slot.rx[slot.rx_head];
    queued = {};
    queued.source = {datagram.source, datagram.source_port};
    queued.size = datagram.payload_length;
    for (size_t index = 0U; index < queued.size; ++index) {
        queued.payload[index] = datagram.payload[index];
    }
    slot.rx_head = (slot.rx_head + 1U) % MAX_RX_DATAGRAMS;
    ++slot.rx_count;
    return true;
}

} // namespace

Status initialize(const Backend& backend) {
    if (g_initialized) return Status::AlreadyInitialized;
    if (backend.send_udp == nullptr || backend.poll == nullptr ||
        backend.take_udp == nullptr) {
        return Status::InvalidArgument;
    }
    for (Slot& slot : g_slots) slot = {};
    g_backend = backend;
    g_next_ephemeral = EPHEMERAL_PORT_FIRST;
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
    if (type != Type::Datagram || protocol != Protocol::Udp) {
        return Status::NotSupported;
    }
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
    if (endpoint_in_use(endpoint, slot)) return Status::AddressInUse;
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
    slot->remote = endpoint;
    slot->connected = true;
    return Status::Ok;
}

Status send(
    ProcessId owner,
    Handle handle,
    const void* data,
    size_t size) {
    if (!g_initialized) return Status::NotInitialized;
    if (data == nullptr || size == 0U) return Status::InvalidArgument;
    if (size > UDP_MAX_PAYLOAD) return Status::PayloadTooLarge;
    Status failure = Status::Ok;
    Slot* slot = resolve(owner, handle, &failure);
    if (slot == nullptr) return failure;
    if (!slot->bound) return Status::NotBound;
    if (!slot->connected) return Status::NotConnected;
    return transport_status(g_backend.send_udp(
        g_backend.context,
        slot->remote.address,
        slot->local.port,
        slot->remote.port,
        static_cast<const uint8_t*>(data),
        size));
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
    clear_slot(*slot);
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

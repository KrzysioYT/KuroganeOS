#include "channel.hpp"

namespace ipc {
namespace {

constexpr uint64_t kIndexMask = UINT64_C(0xFF);
constexpr uint64_t kKindMask = UINT64_C(0xFF) << 16U;
constexpr uint64_t kSideMask = UINT64_C(1) << 24U;
constexpr uint64_t kKindEndpoint = UINT64_C(1) << 16U;
constexpr uint64_t kKindChannel = UINT64_C(2) << 16U;
constexpr uint32_t kMaximumGeneration = UINT32_C(0x7FFFFFFF);

struct MessageQueue {
    Message messages[MAX_MESSAGES_PER_DIRECTION];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
};

struct EndpointSlot {
    uint32_t generation;
    ProcessId owner_pid;
    char name[MAX_SERVICE_NAME + 1U];
    uint8_t pending[MAX_PENDING_CONNECTIONS];
    uint8_t pending_head;
    uint8_t pending_tail;
    uint8_t pending_count;
    ServiceMetadata metadata;
    bool versioned;
    bool active;
};

struct ChannelSlot {
    uint32_t generation;
    ProcessId client_pid;
    ProcessId server_pid;
    MessageQueue to_server;
    MessageQueue to_client;
    bool active;
    bool client_open;
    bool server_open;
    bool accepted;
};

EndpointSlot g_endpoints[MAX_ENDPOINTS]{};
ChannelSlot g_channels[MAX_CHANNELS]{};
bool g_initialized = false;

void clear_bytes(void* destination, size_t size) {
    auto* bytes = static_cast<uint8_t*>(destination);
    for (size_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

uint32_t next_generation(uint32_t generation) {
    return generation == 0U || generation >= kMaximumGeneration
        ? 1U : generation + 1U;
}

bool valid_name_character(char value) {
    return (value >= 'a' && value <= 'z') ||
        (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9') ||
        value == '.' || value == '_' || value == '-';
}

Status validate_name(const char* name, size_t length) {
    if (name == nullptr || length == 0U) return Status::InvalidArgument;
    if (length > MAX_SERVICE_NAME) return Status::NameTooLong;
    for (size_t index = 0U; index < length; ++index) {
        if (name[index] == '\0' || !valid_name_character(name[index])) {
            return Status::InvalidName;
        }
    }
    return Status::Ok;
}

bool name_equal(const EndpointSlot& endpoint, const char* name, size_t length) {
    size_t stored = 0U;
    while (stored <= MAX_SERVICE_NAME && endpoint.name[stored] != '\0') ++stored;
    if (stored != length) return false;
    for (size_t index = 0U; index < length; ++index) {
        if (endpoint.name[index] != name[index]) return false;
    }
    return true;
}

Handle encode_endpoint(size_t index, uint32_t generation) {
    return (static_cast<uint64_t>(generation) << 32U) |
        kKindEndpoint | static_cast<uint64_t>(index + 1U);
}

Handle encode_channel(size_t index, uint32_t generation, bool server_side) {
    return (static_cast<uint64_t>(generation) << 32U) |
        kKindChannel | (server_side ? kSideMask : 0U) |
        static_cast<uint64_t>(index + 1U);
}

bool decode_index(Handle handle, size_t maximum, size_t* index) {
    if (index == nullptr) return false;
    const uint64_t encoded = handle & kIndexMask;
    if (encoded == 0U || encoded > maximum) return false;
    *index = static_cast<size_t>(encoded - 1U);
    return true;
}

Status decode_endpoint(ProcessId owner_pid, Handle handle, EndpointSlot** output) {
    if (output == nullptr) return Status::InvalidArgument;
    *output = nullptr;
    if ((handle & kKindMask) != kKindEndpoint || (handle & kSideMask) != 0U) {
        return Status::StaleHandle;
    }
    size_t index = 0U;
    if (!decode_index(handle, MAX_ENDPOINTS, &index)) return Status::StaleHandle;
    EndpointSlot& slot = g_endpoints[index];
    const uint32_t generation = static_cast<uint32_t>(handle >> 32U);
    if (!slot.active || slot.generation != generation) return Status::StaleHandle;
    if (slot.owner_pid != owner_pid) return Status::AccessDenied;
    *output = &slot;
    return Status::Ok;
}

Status decode_channel(
    ProcessId owner_pid,
    Handle handle,
    ChannelSlot** output,
    bool* server_side) {
    if (output == nullptr || server_side == nullptr) return Status::InvalidArgument;
    *output = nullptr;
    *server_side = false;
    if ((handle & kKindMask) != kKindChannel) return Status::StaleHandle;
    size_t index = 0U;
    if (!decode_index(handle, MAX_CHANNELS, &index)) return Status::StaleHandle;
    ChannelSlot& slot = g_channels[index];
    const uint32_t generation = static_cast<uint32_t>(handle >> 32U);
    if (!slot.active || slot.generation != generation) return Status::StaleHandle;
    const bool server = (handle & kSideMask) != 0U;
    const ProcessId expected = server ? slot.server_pid : slot.client_pid;
    const bool open = server ? slot.server_open : slot.client_open;
    if (expected != owner_pid) return Status::AccessDenied;
    if (!open) return Status::StaleHandle;
    *output = &slot;
    *server_side = server;
    return Status::Ok;
}

void reset_channel(ChannelSlot& slot) {
    const uint32_t generation = slot.generation;
    clear_bytes(&slot, sizeof(slot));
    slot.generation = generation;
}

void maybe_free_channel(ChannelSlot& slot) {
    if (!slot.client_open && !slot.server_open) reset_channel(slot);
}

bool pending_live(const EndpointSlot& endpoint, size_t channel_index) {
    if (channel_index >= MAX_CHANNELS) return false;
    const ChannelSlot& channel = g_channels[channel_index];
    return channel.active && !channel.accepted && channel.client_open &&
        channel.server_open && channel.server_pid == endpoint.owner_pid;
}

void compact_pending(EndpointSlot& endpoint) {
    uint8_t retained[MAX_PENDING_CONNECTIONS]{};
    uint8_t retained_count = 0U;
    for (uint8_t offset = 0U; offset < endpoint.pending_count; ++offset) {
        const uint8_t position = static_cast<uint8_t>(
            (endpoint.pending_head + offset) % MAX_PENDING_CONNECTIONS);
        const uint8_t channel_index = endpoint.pending[position];
        if (pending_live(endpoint, channel_index)) {
            retained[retained_count++] = channel_index;
        }
    }
    for (size_t index = 0U; index < MAX_PENDING_CONNECTIONS; ++index) {
        endpoint.pending[index] = index < retained_count ? retained[index] : 0U;
    }
    endpoint.pending_head = 0U;
    endpoint.pending_tail = static_cast<uint8_t>(
        retained_count % MAX_PENDING_CONNECTIONS);
    endpoint.pending_count = retained_count;
}

void cancel_pending(EndpointSlot& endpoint) {
    compact_pending(endpoint);
    while (endpoint.pending_count != 0U) {
        const size_t channel_index = endpoint.pending[endpoint.pending_head];
        endpoint.pending_head = static_cast<uint8_t>(
            (endpoint.pending_head + 1U) % MAX_PENDING_CONNECTIONS);
        --endpoint.pending_count;
        if (channel_index >= MAX_CHANNELS) continue;
        ChannelSlot& channel = g_channels[channel_index];
        if (!channel.active || channel.accepted ||
            channel.server_pid != endpoint.owner_pid) {
            continue;
        }
        channel.server_open = false;
        maybe_free_channel(channel);
    }
    endpoint.pending_head = 0U;
    endpoint.pending_tail = 0U;
}

Status push_message(MessageQueue& queue, ProcessId sender, const void* data, size_t size) {
    if (queue.count >= MAX_MESSAGES_PER_DIRECTION) return Status::WouldBlock;
    Message& message = queue.messages[queue.tail];
    message = {};
    message.sender_pid = sender;
    message.size = size;
    const auto* source = static_cast<const uint8_t*>(data);
    for (size_t index = 0U; index < size; ++index) message.bytes[index] = source[index];
    queue.tail = static_cast<uint8_t>(
        (queue.tail + 1U) % MAX_MESSAGES_PER_DIRECTION);
    ++queue.count;
    return Status::Ok;
}

Status pop_message(MessageQueue& queue, Message* output) {
    if (queue.count == 0U) return Status::WouldBlock;
    *output = queue.messages[queue.head];
    queue.messages[queue.head] = {};
    queue.head = static_cast<uint8_t>(
        (queue.head + 1U) % MAX_MESSAGES_PER_DIRECTION);
    --queue.count;
    return Status::Ok;
}

} // namespace

Status initialize() {
    if (g_initialized) return Status::AlreadyInitialized;
    clear_bytes(g_endpoints, sizeof(g_endpoints));
    clear_bytes(g_channels, sizeof(g_channels));
    g_initialized = true;
    return Status::Ok;
}

Status bind(
    ProcessId owner_pid,
    const char* name,
    size_t name_length,
    Handle* endpoint,
    const ServiceMetadata* metadata) {
    if (endpoint != nullptr) *endpoint = INVALID_HANDLE;
    if (!g_initialized) return Status::NotInitialized;
    if (owner_pid == INVALID_PROCESS_ID || endpoint == nullptr) {
        return Status::InvalidArgument;
    }
    const Status name_status = validate_name(name, name_length);
    if (name_status != Status::Ok) return name_status;
    if (metadata != nullptr &&
        (metadata->service_version == 0U ||
         metadata->minimum_client_version == 0U ||
         metadata->minimum_client_version > metadata->service_version)) {
        return Status::InvalidArgument;
    }
    for (const EndpointSlot& slot : g_endpoints) {
        if (slot.active && name_equal(slot, name, name_length)) {
            return Status::AlreadyExists;
        }
    }
    for (size_t index = 0U; index < MAX_ENDPOINTS; ++index) {
        EndpointSlot& slot = g_endpoints[index];
        if (slot.active) continue;
        const uint32_t generation = next_generation(slot.generation);
        clear_bytes(&slot, sizeof(slot));
        slot.generation = generation;
        slot.owner_pid = owner_pid;
        for (size_t character = 0U; character < name_length; ++character) {
            slot.name[character] = name[character];
        }
        slot.name[name_length] = '\0';
        if (metadata != nullptr) {
            slot.metadata = *metadata;
            slot.versioned = true;
        }
        slot.active = true;
        *endpoint = encode_endpoint(index, generation);
        return Status::Ok;
    }
    return Status::CapacityReached;
}

Status query(
    const char* name,
    size_t name_length,
    ServiceInfo* info) {
    if (info != nullptr) *info = {};
    if (!g_initialized) return Status::NotInitialized;
    if (info == nullptr) return Status::InvalidArgument;
    const Status name_status = validate_name(name, name_length);
    if (name_status != Status::Ok) return name_status;
    for (const EndpointSlot& slot : g_endpoints) {
        if (!slot.active || !name_equal(slot, name, name_length)) continue;
        if (!slot.versioned) return Status::VersionMismatch;
        info->service_version = slot.metadata.service_version;
        info->minimum_client_version = slot.metadata.minimum_client_version;
        info->capabilities = slot.metadata.capabilities;
        info->owner_pid = slot.owner_pid;
        return Status::Ok;
    }
    return Status::NotFound;
}

Status connect(
    ProcessId client_pid,
    const char* name,
    size_t name_length,
    Handle* channel,
    ServiceNegotiation* negotiation) {
    if (channel != nullptr) *channel = INVALID_HANDLE;
    if (!g_initialized) return Status::NotInitialized;
    if (client_pid == INVALID_PROCESS_ID || channel == nullptr) {
        return Status::InvalidArgument;
    }
    const Status name_status = validate_name(name, name_length);
    if (name_status != Status::Ok) return name_status;
    EndpointSlot* endpoint = nullptr;
    for (EndpointSlot& slot : g_endpoints) {
        if (slot.active && name_equal(slot, name, name_length)) {
            endpoint = &slot;
            break;
        }
    }
    if (endpoint == nullptr) return Status::NotFound;
    uint32_t selected_version = 0U;
    if (negotiation != nullptr) {
        if (negotiation->minimum_version == 0U ||
            negotiation->maximum_version < negotiation->minimum_version) {
            return Status::InvalidArgument;
        }
        if (!endpoint->versioned) return Status::VersionMismatch;
        const uint32_t lower = negotiation->minimum_version >
                endpoint->metadata.minimum_client_version
            ? negotiation->minimum_version
            : endpoint->metadata.minimum_client_version;
        const uint32_t upper = negotiation->maximum_version <
                endpoint->metadata.service_version
            ? negotiation->maximum_version
            : endpoint->metadata.service_version;
        if (lower > upper) return Status::VersionMismatch;
        selected_version = upper;
    }
    compact_pending(*endpoint);
    if (endpoint->pending_count >= MAX_PENDING_CONNECTIONS) return Status::WouldBlock;

    for (size_t index = 0U; index < MAX_CHANNELS; ++index) {
        ChannelSlot& slot = g_channels[index];
        if (slot.active) continue;
        const uint32_t generation = next_generation(slot.generation);
        clear_bytes(&slot, sizeof(slot));
        slot.generation = generation;
        slot.client_pid = client_pid;
        slot.server_pid = endpoint->owner_pid;
        slot.active = true;
        slot.client_open = true;
        slot.server_open = true;
        endpoint->pending[endpoint->pending_tail] = static_cast<uint8_t>(index);
        endpoint->pending_tail = static_cast<uint8_t>(
            (endpoint->pending_tail + 1U) % MAX_PENDING_CONNECTIONS);
        ++endpoint->pending_count;
        *channel = encode_channel(index, generation, false);
        if (negotiation != nullptr) {
            negotiation->selected_version = selected_version;
            negotiation->service_version = endpoint->metadata.service_version;
            negotiation->minimum_client_version =
                endpoint->metadata.minimum_client_version;
            negotiation->capabilities = endpoint->metadata.capabilities;
            negotiation->owner_pid = endpoint->owner_pid;
        }
        return Status::Ok;
    }
    return Status::CapacityReached;
}

Status accept(ProcessId server_pid, Handle endpoint_handle, Handle* channel) {
    if (channel != nullptr) *channel = INVALID_HANDLE;
    if (!g_initialized) return Status::NotInitialized;
    if (server_pid == INVALID_PROCESS_ID || channel == nullptr) {
        return Status::InvalidArgument;
    }
    EndpointSlot* endpoint = nullptr;
    Status status = decode_endpoint(server_pid, endpoint_handle, &endpoint);
    if (status != Status::Ok) return status;
    compact_pending(*endpoint);

    while (endpoint->pending_count != 0U) {
        const size_t index = endpoint->pending[endpoint->pending_head];
        endpoint->pending_head = static_cast<uint8_t>(
            (endpoint->pending_head + 1U) % MAX_PENDING_CONNECTIONS);
        --endpoint->pending_count;
        if (!pending_live(*endpoint, index)) continue;
        ChannelSlot& slot = g_channels[index];
        slot.accepted = true;
        *channel = encode_channel(index, slot.generation, true);
        return Status::Ok;
    }
    endpoint->pending_head = 0U;
    endpoint->pending_tail = 0U;
    return Status::WouldBlock;
}

Status send(
    ProcessId sender_pid,
    Handle channel_handle,
    const void* data,
    size_t size) {
    if (!g_initialized) return Status::NotInitialized;
    if (sender_pid == INVALID_PROCESS_ID || size > MAX_MESSAGE_SIZE ||
        (size != 0U && data == nullptr)) {
        return Status::InvalidArgument;
    }
    ChannelSlot* channel = nullptr;
    bool server_side = false;
    Status status = decode_channel(sender_pid, channel_handle, &channel, &server_side);
    if (status != Status::Ok) return status;
    const bool peer_open = server_side ? channel->client_open : channel->server_open;
    if (!peer_open) return Status::PeerClosed;
    MessageQueue& queue = server_side ? channel->to_client : channel->to_server;
    return push_message(queue, sender_pid, data, size);
}

Status receive(
    ProcessId receiver_pid,
    Handle channel_handle,
    Message* message) {
    if (message != nullptr) *message = {};
    if (!g_initialized) return Status::NotInitialized;
    if (receiver_pid == INVALID_PROCESS_ID || message == nullptr) {
        return Status::InvalidArgument;
    }
    ChannelSlot* channel = nullptr;
    bool server_side = false;
    Status status = decode_channel(
        receiver_pid, channel_handle, &channel, &server_side);
    if (status != Status::Ok) return status;
    MessageQueue& queue = server_side ? channel->to_server : channel->to_client;
    status = pop_message(queue, message);
    if (status == Status::WouldBlock) {
        const bool peer_open = server_side ? channel->client_open : channel->server_open;
        return peer_open ? Status::WouldBlock : Status::PeerClosed;
    }
    return status;
}

Status close(ProcessId owner_pid, Handle handle) {
    if (!g_initialized) return Status::NotInitialized;
    if (owner_pid == INVALID_PROCESS_ID || handle == INVALID_HANDLE) {
        return Status::InvalidArgument;
    }
    if ((handle & kKindMask) == kKindEndpoint) {
        EndpointSlot* endpoint = nullptr;
        const Status status = decode_endpoint(owner_pid, handle, &endpoint);
        if (status != Status::Ok) return status;
        cancel_pending(*endpoint);
        endpoint->active = false;
        endpoint->owner_pid = INVALID_PROCESS_ID;
        endpoint->name[0] = '\0';
        return Status::Ok;
    }
    if ((handle & kKindMask) == kKindChannel) {
        ChannelSlot* channel = nullptr;
        bool server_side = false;
        const Status status = decode_channel(owner_pid, handle, &channel, &server_side);
        if (status != Status::Ok) return status;
        if (server_side) {
            channel->server_open = false;
        } else {
            channel->client_open = false;
            if (!channel->accepted) channel->server_open = false;
        }
        maybe_free_channel(*channel);
        return Status::Ok;
    }
    return Status::StaleHandle;
}

void release_process(ProcessId pid) {
    if (!g_initialized || pid == INVALID_PROCESS_ID) return;
    for (EndpointSlot& endpoint : g_endpoints) {
        if (!endpoint.active || endpoint.owner_pid != pid) continue;
        cancel_pending(endpoint);
        endpoint.active = false;
        endpoint.owner_pid = INVALID_PROCESS_ID;
        endpoint.name[0] = '\0';
    }
    for (ChannelSlot& channel : g_channels) {
        if (!channel.active) continue;
        if (channel.client_pid == pid) {
            channel.client_open = false;
            if (!channel.accepted) channel.server_open = false;
        }
        if (channel.server_pid == pid) channel.server_open = false;
        maybe_free_channel(channel);
    }
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::AlreadyInitialized: return "already initialized";
        case Status::NotInitialized: return "not initialized";
        case Status::InvalidArgument: return "invalid argument";
        case Status::InvalidName: return "invalid service name";
        case Status::NameTooLong: return "service name too long";
        case Status::AlreadyExists: return "service already exists";
        case Status::NotFound: return "service not found";
        case Status::StaleHandle: return "stale IPC handle";
        case Status::AccessDenied: return "IPC handle owner mismatch";
        case Status::CapacityReached: return "IPC capacity reached";
        case Status::WouldBlock: return "IPC operation would block";
        case Status::VersionMismatch: return "service version mismatch";
        case Status::PeerClosed: return "IPC peer closed";
    }
    return "unknown IPC status";
}

} // namespace ipc

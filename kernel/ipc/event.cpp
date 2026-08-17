#include "event.hpp"

namespace ipc::event {
namespace {

constexpr uint64_t kIndexMask = UINT64_C(0xFF);
constexpr uint64_t kKindMask = UINT64_C(0xFF) << 16U;
constexpr uint64_t kKindEvent = UINT64_C(4) << 16U;
constexpr uint32_t kMaximumGeneration = UINT32_C(0x7FFFFFFF);

struct GrantSlot {
    ProcessId pid;
    bool open;
};

struct EventSlot {
    uint32_t generation;
    ProcessId owner_pid;
    ResetMode mode;
    bool signaled;
    bool owner_open;
    bool active;
    GrantSlot grants[MAX_GRANTS_PER_EVENT];
};

EventSlot g_events[MAX_EVENTS]{};
bool g_initialized = false;

void clear_bytes(void* destination, size_t size) {
    auto* bytes = static_cast<uint8_t*>(destination);
    for (size_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

uint32_t next_generation(uint32_t generation) {
    return generation == 0U || generation >= kMaximumGeneration
        ? 1U : generation + 1U;
}

Handle encode(size_t index, uint32_t generation) {
    return (static_cast<uint64_t>(generation) << 32U) |
        kKindEvent | static_cast<uint64_t>(index + 1U);
}

Status decode(Handle handle, EventSlot** output) {
    if (output == nullptr) return Status::InvalidArgument;
    *output = nullptr;
    if ((handle & kKindMask) != kKindEvent) return Status::StaleHandle;
    const uint64_t encoded = handle & kIndexMask;
    if (encoded == 0U || encoded > MAX_EVENTS) return Status::StaleHandle;
    EventSlot& slot = g_events[static_cast<size_t>(encoded - 1U)];
    const uint32_t generation = static_cast<uint32_t>(handle >> 32U);
    if (!slot.active || slot.generation != generation) return Status::StaleHandle;
    *output = &slot;
    return Status::Ok;
}

GrantSlot* find_grant(EventSlot& slot, ProcessId pid) {
    for (GrantSlot& grant : slot.grants) {
        if (grant.open && grant.pid == pid) return &grant;
    }
    return nullptr;
}

bool can_access(EventSlot& slot, ProcessId pid) {
    return (slot.owner_pid == pid && slot.owner_open) || find_grant(slot, pid) != nullptr;
}

bool has_open_reference(const EventSlot& slot) {
    if (slot.owner_open) return true;
    for (const GrantSlot& grant : slot.grants) {
        if (grant.open) return true;
    }
    return false;
}

void reclaim(EventSlot& slot) {
    const uint32_t generation = slot.generation;
    clear_bytes(&slot, sizeof(slot));
    slot.generation = generation;
}

void maybe_reclaim(EventSlot& slot) {
    if (slot.active && !has_open_reference(slot)) reclaim(slot);
}

} // namespace

Status initialize() {
    if (g_initialized) return Status::AlreadyInitialized;
    clear_bytes(g_events, sizeof(g_events));
    g_initialized = true;
    return Status::Ok;
}

Status create(ProcessId owner_pid, ResetMode mode, bool signaled, Handle* handle) {
    if (handle != nullptr) *handle = INVALID_HANDLE;
    if (!g_initialized) return Status::NotInitialized;
    if (owner_pid == INVALID_PROCESS_ID || handle == nullptr ||
        (mode != ResetMode::Auto && mode != ResetMode::Manual)) {
        return Status::InvalidArgument;
    }
    for (size_t index = 0U; index < MAX_EVENTS; ++index) {
        EventSlot& slot = g_events[index];
        if (slot.active) continue;
        const uint32_t generation = next_generation(slot.generation);
        clear_bytes(&slot, sizeof(slot));
        slot.generation = generation;
        slot.owner_pid = owner_pid;
        slot.mode = mode;
        slot.signaled = signaled;
        slot.owner_open = true;
        slot.active = true;
        *handle = encode(index, generation);
        return Status::Ok;
    }
    return Status::CapacityReached;
}

Status grant(ProcessId owner_pid, Handle handle, ProcessId target_pid) {
    if (!g_initialized) return Status::NotInitialized;
    if (owner_pid == INVALID_PROCESS_ID || target_pid == INVALID_PROCESS_ID ||
        owner_pid == target_pid) return Status::InvalidArgument;
    EventSlot* slot = nullptr;
    const Status status = decode(handle, &slot);
    if (status != Status::Ok) return status;
    if (slot->owner_pid != owner_pid || !slot->owner_open) return Status::AccessDenied;
    for (GrantSlot& grant_slot : slot->grants) {
        if (grant_slot.open && grant_slot.pid == target_pid) return Status::AlreadyGranted;
    }
    for (GrantSlot& grant_slot : slot->grants) {
        if (!grant_slot.open) {
            grant_slot.pid = target_pid;
            grant_slot.open = true;
            return Status::Ok;
        }
    }
    return Status::CapacityReached;
}

Status signal(ProcessId pid, Handle handle) {
    if (!g_initialized) return Status::NotInitialized;
    if (pid == INVALID_PROCESS_ID) return Status::InvalidArgument;
    EventSlot* slot = nullptr;
    const Status status = decode(handle, &slot);
    if (status != Status::Ok) return status;
    if (!can_access(*slot, pid)) return Status::AccessDenied;
    slot->signaled = true;
    return Status::Ok;
}

Status reset(ProcessId pid, Handle handle) {
    if (!g_initialized) return Status::NotInitialized;
    if (pid == INVALID_PROCESS_ID) return Status::InvalidArgument;
    EventSlot* slot = nullptr;
    const Status status = decode(handle, &slot);
    if (status != Status::Ok) return status;
    if (!can_access(*slot, pid)) return Status::AccessDenied;
    slot->signaled = false;
    return Status::Ok;
}

Status poll(ProcessId pid, Handle handle) {
    if (!g_initialized) return Status::NotInitialized;
    if (pid == INVALID_PROCESS_ID) return Status::InvalidArgument;
    EventSlot* slot = nullptr;
    const Status status = decode(handle, &slot);
    if (status != Status::Ok) return status;
    if (!can_access(*slot, pid)) return Status::AccessDenied;
    if (!slot->signaled) return Status::WouldBlock;
    if (slot->mode == ResetMode::Auto) slot->signaled = false;
    return Status::Ok;
}

Status close(ProcessId pid, Handle handle) {
    if (!g_initialized) return Status::NotInitialized;
    if (pid == INVALID_PROCESS_ID) return Status::InvalidArgument;
    EventSlot* slot = nullptr;
    const Status status = decode(handle, &slot);
    if (status != Status::Ok) return status;
    if (slot->owner_pid == pid && slot->owner_open) {
        slot->owner_open = false;
        maybe_reclaim(*slot);
        return Status::Ok;
    }
    GrantSlot* grant = find_grant(*slot, pid);
    if (grant == nullptr) return Status::AccessDenied;
    *grant = {};
    maybe_reclaim(*slot);
    return Status::Ok;
}

void release_process(ProcessId pid) {
    if (!g_initialized || pid == INVALID_PROCESS_ID) return;
    for (EventSlot& slot : g_events) {
        if (!slot.active) continue;
        if (slot.owner_pid == pid) slot.owner_open = false;
        for (GrantSlot& grant : slot.grants) {
            if (grant.open && grant.pid == pid) grant = {};
        }
        maybe_reclaim(slot);
    }
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::AlreadyInitialized: return "already initialized";
        case Status::NotInitialized: return "not initialized";
        case Status::InvalidArgument: return "invalid argument";
        case Status::CapacityReached: return "capacity reached";
        case Status::StaleHandle: return "stale handle";
        case Status::AccessDenied: return "access denied";
        case Status::AlreadyGranted: return "already granted";
        case Status::WouldBlock: return "would block";
    }
    return "unknown event status";
}

} // namespace ipc::event

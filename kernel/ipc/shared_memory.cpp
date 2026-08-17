#include "shared_memory.hpp"

#include "../memory/physical_memory.hpp"

namespace ipc::shared_memory {
namespace {

constexpr uint64_t kIndexMask = UINT64_C(0xFF);
constexpr uint64_t kKindMask = UINT64_C(0xFF) << 16U;
constexpr uint64_t kKindSharedMemory = UINT64_C(3) << 16U;
constexpr uint32_t kMaximumGeneration = UINT32_C(0x7FFFFFFF);

struct AccessSlot {
    ProcessId pid;
    uint16_t mappings;
    bool open;
};

struct ObjectSlot {
    uint32_t generation;
    ProcessId owner_pid;
    void* frames[MAX_PAGES_PER_OBJECT];
    size_t page_count;
    size_t size;
    uint16_t owner_mappings;
    bool active;
    bool owner_open;
    AccessSlot grants[MAX_GRANTS_PER_OBJECT];
};

ObjectSlot g_objects[MAX_OBJECTS]{};
bool g_initialized = false;

void clear_bytes(void* destination, size_t size) {
    auto* bytes = static_cast<uint8_t*>(destination);
    for (size_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

void zero_frame(void* frame) {
    clear_bytes(frame, PAGE_SIZE);
}

uint32_t next_generation(uint32_t generation) {
    return generation == 0U || generation >= kMaximumGeneration
        ? 1U : generation + 1U;
}

Handle encode(size_t index, uint32_t generation) {
    return (static_cast<uint64_t>(generation) << 32U) |
        kKindSharedMemory | static_cast<uint64_t>(index + 1U);
}

Status decode(Handle handle, ObjectSlot** output) {
    if (output == nullptr) return Status::InvalidArgument;
    *output = nullptr;
    if ((handle & kKindMask) != kKindSharedMemory) return Status::StaleHandle;
    const uint64_t encoded = handle & kIndexMask;
    if (encoded == 0U || encoded > MAX_OBJECTS) return Status::StaleHandle;
    ObjectSlot& slot = g_objects[static_cast<size_t>(encoded - 1U)];
    const uint32_t generation = static_cast<uint32_t>(handle >> 32U);
    if (!slot.active || slot.generation != generation) return Status::StaleHandle;
    *output = &slot;
    return Status::Ok;
}

AccessSlot* find_grant(ObjectSlot& slot, ProcessId pid) {
    for (AccessSlot& grant : slot.grants) {
        if (grant.pid == pid && (grant.open || grant.mappings != 0U)) return &grant;
    }
    return nullptr;
}

bool has_live_access(const ObjectSlot& slot) {
    if (slot.owner_open || slot.owner_mappings != 0U) return true;
    for (const AccessSlot& grant : slot.grants) {
        if (grant.open || grant.mappings != 0U) return true;
    }
    return false;
}

void reclaim(ObjectSlot& slot) {
    for (size_t index = 0U; index < slot.page_count; ++index) {
        if (slot.frames[index] != nullptr) memory::free_frame(slot.frames[index]);
    }
    const uint32_t generation = slot.generation;
    clear_bytes(&slot, sizeof(slot));
    slot.generation = generation;
}

void maybe_reclaim(ObjectSlot& slot) {
    if (slot.active && !has_live_access(slot)) reclaim(slot);
}

Status access_slot(ObjectSlot& slot, ProcessId pid, uint16_t** mappings, bool** open) {
    if (pid == slot.owner_pid) {
        if (mappings != nullptr) *mappings = &slot.owner_mappings;
        if (open != nullptr) *open = &slot.owner_open;
        return slot.owner_open || slot.owner_mappings != 0U
            ? Status::Ok : Status::AccessDenied;
    }
    AccessSlot* grant = find_grant(slot, pid);
    if (grant == nullptr) return Status::AccessDenied;
    if (mappings != nullptr) *mappings = &grant->mappings;
    if (open != nullptr) *open = &grant->open;
    return Status::Ok;
}

} // namespace

Status initialize() {
    if (g_initialized) return Status::AlreadyInitialized;
    clear_bytes(g_objects, sizeof(g_objects));
    g_initialized = true;
    return Status::Ok;
}

Status create(ProcessId owner_pid, size_t size, Handle* handle) {
    if (handle != nullptr) *handle = INVALID_HANDLE;
    if (!g_initialized) return Status::NotInitialized;
    if (owner_pid == INVALID_PROCESS_ID || handle == nullptr || size == 0U) {
        return Status::InvalidArgument;
    }
    if (size > MAX_PAGES_PER_OBJECT * PAGE_SIZE) return Status::OutOfRange;
    const size_t page_count = (size + PAGE_SIZE - 1U) / PAGE_SIZE;

    size_t object_index = MAX_OBJECTS;
    for (size_t index = 0U; index < MAX_OBJECTS; ++index) {
        if (!g_objects[index].active) {
            object_index = index;
            break;
        }
    }
    if (object_index == MAX_OBJECTS) return Status::CapacityReached;

    ObjectSlot& slot = g_objects[object_index];
    const uint32_t generation = next_generation(slot.generation);
    clear_bytes(&slot, sizeof(slot));
    slot.generation = generation;
    slot.owner_pid = owner_pid;
    slot.page_count = page_count;
    slot.size = size;
    slot.owner_open = true;

    for (size_t index = 0U; index < page_count; ++index) {
        slot.frames[index] = memory::alloc_frame();
        if (slot.frames[index] == nullptr) {
            for (size_t previous = 0U; previous < index; ++previous) {
                memory::free_frame(slot.frames[previous]);
            }
            const uint32_t retained_generation = slot.generation;
            clear_bytes(&slot, sizeof(slot));
            slot.generation = retained_generation;
            return Status::OutOfMemory;
        }
        zero_frame(slot.frames[index]);
    }

    slot.active = true;
    *handle = encode(object_index, slot.generation);
    return Status::Ok;
}

Status grant(ProcessId owner_pid, Handle handle, ProcessId target_pid) {
    if (!g_initialized) return Status::NotInitialized;
    if (owner_pid == INVALID_PROCESS_ID || target_pid == INVALID_PROCESS_ID ||
        owner_pid == target_pid) return Status::InvalidArgument;
    ObjectSlot* slot = nullptr;
    const Status decode_status = decode(handle, &slot);
    if (decode_status != Status::Ok) return decode_status;
    if (slot->owner_pid != owner_pid || !slot->owner_open) return Status::AccessDenied;

    for (AccessSlot& grant_slot : slot->grants) {
        if (grant_slot.pid != target_pid) continue;
        if (grant_slot.open) return Status::AlreadyGranted;
        if (grant_slot.mappings != 0U) {
            grant_slot.open = true;
            return Status::Ok;
        }
    }
    for (AccessSlot& grant_slot : slot->grants) {
        if (grant_slot.pid == INVALID_PROCESS_ID && grant_slot.mappings == 0U) {
            grant_slot.pid = target_pid;
            grant_slot.open = true;
            return Status::Ok;
        }
    }
    return Status::CapacityReached;
}

Status acquire(ProcessId pid, Handle handle, View* view) {
    if (view != nullptr) *view = {};
    if (!g_initialized) return Status::NotInitialized;
    if (pid == INVALID_PROCESS_ID || view == nullptr) return Status::InvalidArgument;
    ObjectSlot* slot = nullptr;
    const Status decode_status = decode(handle, &slot);
    if (decode_status != Status::Ok) return decode_status;
    uint16_t* mappings = nullptr;
    bool* open = nullptr;
    const Status access = access_slot(*slot, pid, &mappings, &open);
    if (access != Status::Ok || open == nullptr || !*open) return Status::AccessDenied;
    if (*mappings == UINT16_MAX) return Status::Busy;
    ++*mappings;
    view->page_count = slot->page_count;
    view->size = slot->size;
    for (size_t index = 0U; index < slot->page_count; ++index) {
        view->frames[index] = slot->frames[index];
    }
    return Status::Ok;
}

Status release(ProcessId pid, Handle handle) {
    if (!g_initialized) return Status::NotInitialized;
    if (pid == INVALID_PROCESS_ID) return Status::InvalidArgument;
    ObjectSlot* slot = nullptr;
    const Status decode_status = decode(handle, &slot);
    if (decode_status != Status::Ok) return decode_status;
    uint16_t* mappings = nullptr;
    const Status access = access_slot(*slot, pid, &mappings, nullptr);
    if (access != Status::Ok || mappings == nullptr || *mappings == 0U) {
        return Status::NotFound;
    }
    --*mappings;
    maybe_reclaim(*slot);
    return Status::Ok;
}

Status close(ProcessId pid, Handle handle) {
    if (!g_initialized) return Status::NotInitialized;
    if (pid == INVALID_PROCESS_ID) return Status::InvalidArgument;
    ObjectSlot* slot = nullptr;
    const Status decode_status = decode(handle, &slot);
    if (decode_status != Status::Ok) return decode_status;
    bool* open = nullptr;
    const Status access = access_slot(*slot, pid, nullptr, &open);
    if (access != Status::Ok || open == nullptr || !*open) return Status::AccessDenied;
    *open = false;
    maybe_reclaim(*slot);
    return Status::Ok;
}

void release_process(ProcessId pid) {
    if (!g_initialized || pid == INVALID_PROCESS_ID) return;
    for (ObjectSlot& slot : g_objects) {
        if (!slot.active) continue;
        if (slot.owner_pid == pid) {
            slot.owner_open = false;
            slot.owner_mappings = 0U;
        }
        for (AccessSlot& grant : slot.grants) {
            if (grant.pid != pid) continue;
            grant.open = false;
            grant.mappings = 0U;
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
        case Status::OutOfRange: return "out of range";
        case Status::OutOfMemory: return "out of memory";
        case Status::CapacityReached: return "capacity reached";
        case Status::StaleHandle: return "stale handle";
        case Status::AccessDenied: return "access denied";
        case Status::AlreadyGranted: return "already granted";
        case Status::NotFound: return "not found";
        case Status::Busy: return "busy";
    }
    return "unknown shared-memory status";
}

} // namespace ipc::shared_memory

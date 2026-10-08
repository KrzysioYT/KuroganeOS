#include "job.hpp"

#include "../sync/spinlock.hpp"

namespace process::job {
namespace {

constexpr uint64_t kIndexMask = UINT64_C(0xFF);
constexpr uint64_t kMaximumGeneration = UINT64_MAX >> 8U;

struct Slot {
    JobId id;
    uint64_t generation;
    ProcessId owner_pid;
    ProcessId members[MAX_JOB_MEMBERS];
    size_t member_count;
    bool active;
    char name[MAX_JOB_NAME + 1U];
};

Slot g_slots[MAX_JOBS]{};
bool g_initialized = false;
sync::TicketSpinLock g_lock{};

void clear_bytes(void* destination, size_t size) {
    auto* bytes = static_cast<uint8_t*>(destination);
    for (size_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

size_t text_length(const char* text, size_t limit) {
    if (text == nullptr) return limit + 1U;
    for (size_t index = 0U; index <= limit; ++index) {
        if (text[index] == '\0') return index;
    }
    return limit + 1U;
}

uint64_t next_generation(uint64_t generation) {
    return generation == 0U || generation >= kMaximumGeneration
        ? UINT64_C(1)
        : generation + UINT64_C(1);
}

JobId encode(size_t index, uint64_t generation) {
    return (generation << 8U) | static_cast<uint64_t>(index + 1U);
}

Slot* decode(JobId id) {
    if (id == INVALID_JOB_ID) return nullptr;
    const uint64_t encoded = id & kIndexMask;
    if (encoded == 0U || encoded > MAX_JOBS) return nullptr;
    Slot& slot = g_slots[static_cast<size_t>(encoded - 1U)];
    if (!slot.active || slot.id != id) return nullptr;
    return &slot;
}

bool has_member(const Slot& slot, ProcessId pid) {
    for (size_t index = 0U; index < slot.member_count; ++index) {
        if (slot.members[index] == pid) return true;
    }
    return false;
}

Status add_member(Slot& slot, ProcessId pid) {
    if (pid == INVALID_PROCESS_ID) return Status::InvalidArgument;
    if (has_member(slot, pid)) return Status::AlreadyExists;
    if (slot.member_count >= MAX_JOB_MEMBERS) return Status::CapacityReached;
    slot.members[slot.member_count++] = pid;
    return Status::Ok;
}

bool remove_member(Slot& slot, ProcessId pid) {
    for (size_t index = 0U; index < slot.member_count; ++index) {
        if (slot.members[index] != pid) continue;
        for (size_t move = index + 1U; move < slot.member_count; ++move) {
            slot.members[move - 1U] = slot.members[move];
        }
        --slot.member_count;
        slot.members[slot.member_count] = INVALID_PROCESS_ID;
        return true;
    }
    return false;
}

void reclaim_if_empty(Slot& slot) {
    if (!slot.active || slot.member_count != 0U) return;
    const uint64_t generation = slot.generation;
    clear_bytes(&slot, sizeof(slot));
    slot.generation = generation;
}

} // namespace

Status initialize() {
    sync::LockGuard guard(g_lock);
    if (g_initialized) return Status::AlreadyInitialized;
    clear_bytes(g_slots, sizeof(g_slots));
    g_initialized = true;
    return Status::Ok;
}

Status create(ProcessId owner_pid, const char* name, JobId* out_job) {
    if (out_job != nullptr) *out_job = INVALID_JOB_ID;
    sync::LockGuard guard(g_lock);
    if (!g_initialized) return Status::NotInitialized;
    if (owner_pid == INVALID_PROCESS_ID || out_job == nullptr) {
        return Status::InvalidArgument;
    }
    const size_t length = text_length(name, MAX_JOB_NAME);
    if (length == 0U) return Status::InvalidArgument;
    if (length > MAX_JOB_NAME) return Status::NameTooLong;

    for (size_t index = 0U; index < MAX_JOBS; ++index) {
        Slot& slot = g_slots[index];
        if (slot.active) continue;
        const uint64_t generation = next_generation(slot.generation);
        clear_bytes(&slot, sizeof(slot));
        slot.generation = generation;
        slot.id = encode(index, generation);
        slot.owner_pid = owner_pid;
        slot.members[0] = owner_pid;
        slot.member_count = 1U;
        slot.active = true;
        for (size_t character = 0U; character < length; ++character) {
            slot.name[character] = name[character];
        }
        slot.name[length] = '\0';
        *out_job = slot.id;
        return Status::Ok;
    }
    return Status::CapacityReached;
}

Status attach(ProcessId owner_pid, JobId id, ProcessId target_pid) {
    sync::LockGuard guard(g_lock);
    if (!g_initialized) return Status::NotInitialized;
    if (owner_pid == INVALID_PROCESS_ID || target_pid == INVALID_PROCESS_ID) {
        return Status::InvalidArgument;
    }
    Slot* slot = decode(id);
    if (slot == nullptr) return Status::NotFound;
    if (slot->owner_pid != owner_pid) return Status::AccessDenied;
    return add_member(*slot, target_pid);
}

Status detach(ProcessId owner_pid, JobId id, ProcessId target_pid) {
    sync::LockGuard guard(g_lock);
    if (!g_initialized) return Status::NotInitialized;
    if (owner_pid == INVALID_PROCESS_ID || target_pid == INVALID_PROCESS_ID) {
        return Status::InvalidArgument;
    }
    Slot* slot = decode(id);
    if (slot == nullptr) return Status::NotFound;
    if (slot->owner_pid != owner_pid || target_pid == slot->owner_pid) {
        return Status::AccessDenied;
    }
    return remove_member(*slot, target_pid) ? Status::Ok : Status::NotFound;
}

Status inherit_member(JobId id, ProcessId child_pid) {
    sync::LockGuard guard(g_lock);
    if (!g_initialized) return Status::NotInitialized;
    if (child_pid == INVALID_PROCESS_ID) return Status::InvalidArgument;
    Slot* slot = decode(id);
    if (slot == nullptr) return Status::NotFound;
    return add_member(*slot, child_pid);
}

void release_process(ProcessId pid) {
    if (pid == INVALID_PROCESS_ID) return;
    sync::LockGuard guard(g_lock);
    if (!g_initialized) return;
    for (Slot& slot : g_slots) {
        if (!slot.active) continue;
        static_cast<void>(remove_member(slot, pid));
        if (slot.owner_pid == pid) slot.owner_pid = INVALID_PROCESS_ID;
        reclaim_if_empty(slot);
    }
}

Status stat(JobId id, Stat* out_stat) {
    if (out_stat != nullptr) *out_stat = {};
    sync::LockGuard guard(g_lock);
    if (!g_initialized) return Status::NotInitialized;
    if (out_stat == nullptr) return Status::InvalidArgument;
    Slot* slot = decode(id);
    if (slot == nullptr) return Status::NotFound;
    out_stat->id = slot->id;
    out_stat->owner_pid = slot->owner_pid;
    out_stat->member_count = slot->member_count;
    out_stat->orphaned = slot->owner_pid == INVALID_PROCESS_ID;
    for (size_t index = 0U; index <= MAX_JOB_NAME; ++index) {
        out_stat->name[index] = slot->name[index];
        if (slot->name[index] == '\0') break;
    }
    return Status::Ok;
}

bool contains(JobId id, ProcessId pid) {
    sync::LockGuard guard(g_lock);
    if (!g_initialized || pid == INVALID_PROCESS_ID) return false;
    Slot* slot = decode(id);
    return slot != nullptr && has_member(*slot, pid);
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::NotInitialized: return "job table not initialized";
        case Status::AlreadyInitialized: return "job table already initialized";
        case Status::InvalidArgument: return "invalid job argument";
        case Status::NameTooLong: return "job name too long";
        case Status::AlreadyExists: return "process already belongs to job";
        case Status::NotFound: return "job or member not found";
        case Status::AccessDenied: return "job owner mismatch";
        case Status::CapacityReached: return "job capacity reached";
    }
    return "unknown job status";
}

} // namespace process::job

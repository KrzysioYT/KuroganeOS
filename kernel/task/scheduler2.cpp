#include "scheduler2.hpp"

#include "../sync/spinlock.hpp"

namespace threading::scheduler2 {
namespace {

constexpr size_t kInvalidIndex = static_cast<size_t>(-1);

struct ThreadSlot {
    ThreadStat stat;
    bool used;
};

struct RunQueue {
    ThreadId ids[MAX_SCHEDULABLE_THREADS];
    size_t count;
    size_t cursor;
    uint64_t dispatches;
    uint64_t steals;
};

ThreadSlot g_threads[MAX_SCHEDULABLE_THREADS]{};
RunQueue g_queues[MAX_CPUS]{};
size_t g_cpu_count = 0U;
bool g_initialized = false;
sync::TicketSpinLock g_policy_lock{};

void clear_bytes(void* destination, size_t size) {
    auto* bytes = static_cast<uint8_t*>(destination);
    for (size_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

CpuMask mask_for_cpu(size_t cpu) {
    return cpu < MAX_CPUS ? (UINT64_C(1) << cpu) : CpuMask{0U};
}

CpuMask online_mask_unlocked() {
    if (!g_initialized || g_cpu_count == 0U) return 0U;
    return g_cpu_count == MAX_CPUS
        ? UINT64_MAX
        : ((UINT64_C(1) << g_cpu_count) - UINT64_C(1));
}

bool cpu_allowed(CpuMask affinity, size_t cpu) {
    return cpu < g_cpu_count && (affinity & mask_for_cpu(cpu)) != 0U;
}

size_t find_thread(ThreadId id) {
    if (id == INVALID_THREAD_ID) return kInvalidIndex;
    for (size_t index = 0U; index < MAX_SCHEDULABLE_THREADS; ++index) {
        if (g_threads[index].used && g_threads[index].stat.id == id) return index;
    }
    return kInvalidIndex;
}

bool queue_contains(const RunQueue& queue, ThreadId id) {
    for (size_t index = 0U; index < queue.count; ++index) {
        if (queue.ids[index] == id) return true;
    }
    return false;
}

Status queue_add(size_t cpu, ThreadId id) {
    if (cpu >= g_cpu_count) return Status::InvalidCpu;
    RunQueue& queue = g_queues[cpu];
    if (queue_contains(queue, id)) return Status::Ok;
    if (queue.count >= MAX_SCHEDULABLE_THREADS) return Status::QueueFull;
    queue.ids[queue.count++] = id;
    if (queue.count == 1U) queue.cursor = 0U;
    return Status::Ok;
}

void queue_remove(size_t cpu, ThreadId id) {
    if (cpu >= g_cpu_count) return;
    RunQueue& queue = g_queues[cpu];
    for (size_t index = 0U; index < queue.count; ++index) {
        if (queue.ids[index] != id) continue;
        for (size_t move = index + 1U; move < queue.count; ++move) {
            queue.ids[move - 1U] = queue.ids[move];
        }
        --queue.count;
        if (queue.count == 0U) {
            queue.cursor = 0U;
        } else if (queue.cursor >= queue.count) {
            queue.cursor %= queue.count;
        }
        return;
    }
}

size_t choose_home(CpuMask affinity, size_t preferred_cpu) {
    if (preferred_cpu < g_cpu_count && cpu_allowed(affinity, preferred_cpu)) {
        return preferred_cpu;
    }
    size_t best_cpu = kInvalidIndex;
    size_t best_load = MAX_SCHEDULABLE_THREADS + 1U;
    for (size_t cpu = 0U; cpu < g_cpu_count; ++cpu) {
        if (!cpu_allowed(affinity, cpu)) continue;
        if (g_queues[cpu].count < best_load) {
            best_cpu = cpu;
            best_load = g_queues[cpu].count;
        }
    }
    return best_cpu;
}

size_t select_from_queue(size_t cpu, ThreadId excluded) {
    RunQueue& queue = g_queues[cpu];
    if (queue.count == 0U) return kInvalidIndex;

    size_t selected_thread = kInvalidIndex;
    size_t selected_queue_offset = kInvalidIndex;
    uint8_t selected_priority = 0U;
    bool have_priority = false;

    for (size_t offset = 0U; offset < queue.count; ++offset) {
        const size_t queue_index = (queue.cursor + offset) % queue.count;
        const ThreadId id = queue.ids[queue_index];
        if (id == excluded) continue;
        const size_t thread_index = find_thread(id);
        if (thread_index == kInvalidIndex) continue;
        const ThreadStat& stat = g_threads[thread_index].stat;
        if (stat.state != State::Ready || !cpu_allowed(stat.affinity, cpu)) {
            continue;
        }
        if (!have_priority || stat.priority > selected_priority) {
            selected_thread = thread_index;
            selected_queue_offset = queue_index;
            selected_priority = stat.priority;
            have_priority = true;
        }
    }

    if (selected_thread != kInvalidIndex) {
        queue.cursor = (selected_queue_offset + 1U) % queue.count;
    }
    return selected_thread;
}

size_t select_steal_candidate(size_t destination_cpu, ThreadId excluded) {
    size_t selected = kInvalidIndex;
    uint8_t selected_priority = 0U;
    bool have_priority = false;
    for (size_t cpu = 0U; cpu < g_cpu_count; ++cpu) {
        if (cpu == destination_cpu) continue;
        const RunQueue& queue = g_queues[cpu];
        for (size_t offset = 0U; offset < queue.count; ++offset) {
            const size_t queue_index = (queue.cursor + offset) % queue.count;
            const size_t thread_index = find_thread(queue.ids[queue_index]);
            if (thread_index == kInvalidIndex) continue;
            const ThreadStat& stat = g_threads[thread_index].stat;
            if (stat.id == excluded || stat.state != State::Ready ||
                !cpu_allowed(stat.affinity, destination_cpu)) {
                continue;
            }
            if (!have_priority || stat.priority > selected_priority) {
                selected = thread_index;
                selected_priority = stat.priority;
                have_priority = true;
            }
        }
    }
    return selected;
}

Status configure(size_t online_cpus) {
    if (online_cpus == 0U || online_cpus > MAX_CPUS) {
        return Status::InvalidArgument;
    }
    clear_bytes(g_threads, sizeof(g_threads));
    clear_bytes(g_queues, sizeof(g_queues));
    g_cpu_count = online_cpus;
    g_initialized = true;
    return Status::Ok;
}

} // namespace

Status initialize(size_t online_cpus) {
    sync::LockGuard guard(g_policy_lock);
    if (g_initialized) return Status::AlreadyInitialized;
    return configure(online_cpus);
}

Status reset(size_t online_cpus) {
    sync::LockGuard guard(g_policy_lock);
    return configure(online_cpus);
}

Status expand_cpu_count(size_t online_cpus) {
    sync::LockGuard guard(g_policy_lock);
    if (!g_initialized) return Status::NotInitialized;
    if (online_cpus == 0U || online_cpus > MAX_CPUS ||
        online_cpus < g_cpu_count) {
        return Status::InvalidArgument;
    }
    if (online_cpus == g_cpu_count) return Status::Ok;
    for (size_t cpu = g_cpu_count; cpu < online_cpus; ++cpu) {
        g_queues[cpu] = {};
    }
    g_cpu_count = online_cpus;
    return Status::Ok;
}

Status register_thread(
    ThreadId id,
    uint8_t priority,
    CpuMask affinity,
    size_t preferred_cpu) {
    sync::LockGuard guard(g_policy_lock);
    if (!g_initialized) return Status::NotInitialized;
    if (id == INVALID_THREAD_ID || priority > MAX_PRIORITY) {
        return Status::InvalidArgument;
    }
    const CpuMask valid_affinity = affinity & online_mask_unlocked();
    if (valid_affinity == 0U || valid_affinity != affinity) {
        return Status::InvalidAffinity;
    }
    if (find_thread(id) != kInvalidIndex) return Status::AlreadyExists;

    size_t free_slot = kInvalidIndex;
    for (size_t index = 0U; index < MAX_SCHEDULABLE_THREADS; ++index) {
        if (!g_threads[index].used) {
            free_slot = index;
            break;
        }
    }
    if (free_slot == kInvalidIndex) return Status::CapacityReached;

    const size_t home = choose_home(affinity, preferred_cpu);
    if (home == kInvalidIndex) return Status::InvalidAffinity;
    if (queue_add(home, id) != Status::Ok) return Status::QueueFull;

    ThreadSlot& slot = g_threads[free_slot];
    slot.used = true;
    slot.stat = {};
    slot.stat.id = id;
    slot.stat.state = State::Ready;
    slot.stat.priority = priority;
    slot.stat.affinity = affinity;
    slot.stat.home_cpu = home;
    slot.stat.last_cpu = MAX_CPUS;
    return Status::Ok;
}

Status unregister_thread(ThreadId id) {
    sync::LockGuard guard(g_policy_lock);
    if (!g_initialized) return Status::NotInitialized;
    const size_t index = find_thread(id);
    if (index == kInvalidIndex) return Status::NotFound;
    queue_remove(g_threads[index].stat.home_cpu, id);
    g_threads[index] = {};
    return Status::Ok;
}

Status set_state(ThreadId id, State state) {
    sync::LockGuard guard(g_policy_lock);
    if (!g_initialized) return Status::NotInitialized;
    const size_t index = find_thread(id);
    if (index == kInvalidIndex) return Status::NotFound;
    g_threads[index].stat.state = state;
    return Status::Ok;
}

Status set_affinity(ThreadId id, CpuMask affinity) {
    sync::LockGuard guard(g_policy_lock);
    if (!g_initialized) return Status::NotInitialized;
    const CpuMask valid_affinity = affinity & online_mask_unlocked();
    if (valid_affinity == 0U || valid_affinity != affinity) {
        return Status::InvalidAffinity;
    }
    const size_t index = find_thread(id);
    if (index == kInvalidIndex) return Status::NotFound;
    ThreadStat& stat = g_threads[index].stat;
    stat.affinity = affinity;
    if (cpu_allowed(affinity, stat.home_cpu)) return Status::Ok;

    const size_t destination = choose_home(affinity, MAX_CPUS);
    if (destination == kInvalidIndex) return Status::InvalidAffinity;
    const size_t previous = stat.home_cpu;
    queue_remove(previous, id);
    const Status add_status = queue_add(destination, id);
    if (add_status != Status::Ok) {
        static_cast<void>(queue_add(previous, id));
        stat.affinity |= mask_for_cpu(previous);
        return add_status;
    }
    stat.home_cpu = destination;
    ++stat.migrations;
    return Status::Ok;
}

Status set_priority(ThreadId id, uint8_t priority) {
    sync::LockGuard guard(g_policy_lock);
    if (!g_initialized) return Status::NotInitialized;
    if (priority > MAX_PRIORITY) return Status::InvalidArgument;
    const size_t index = find_thread(id);
    if (index == kInvalidIndex) return Status::NotFound;
    g_threads[index].stat.priority = priority;
    return Status::Ok;
}

Status pick_next(size_t cpu, ThreadId excluded, ThreadId* out_id) {
    sync::LockGuard guard(g_policy_lock);
    if (out_id != nullptr) *out_id = INVALID_THREAD_ID;
    if (!g_initialized) return Status::NotInitialized;
    if (out_id == nullptr) return Status::InvalidArgument;
    if (cpu >= g_cpu_count) return Status::InvalidCpu;

    size_t selected = select_from_queue(cpu, excluded);
    if (selected == kInvalidIndex) {
        selected = select_steal_candidate(cpu, excluded);
        if (selected != kInvalidIndex) {
            ThreadStat& stat = g_threads[selected].stat;
            const size_t donor = stat.home_cpu;
            queue_remove(donor, stat.id);
            const Status add_status = queue_add(cpu, stat.id);
            if (add_status != Status::Ok) {
                static_cast<void>(queue_add(donor, stat.id));
                return add_status;
            }
            stat.home_cpu = cpu;
            ++stat.migrations;
            ++g_queues[cpu].steals;
            selected = select_from_queue(cpu, excluded);
        }
    }
    if (selected == kInvalidIndex) return Status::NotFound;

    ThreadStat& stat = g_threads[selected].stat;
    // Selection is also a reservation. Under the policy lock this prevents a
    // second CPU from selecting the same Ready thread before the thread layer
    // has finished installing its interrupt frame.
    stat.state = State::Running;
    stat.last_cpu = cpu;
    ++stat.dispatches;
    ++g_queues[cpu].dispatches;
    *out_id = stat.id;
    return Status::Ok;
}

Status cancel_dispatch(ThreadId id, size_t cpu) {
    sync::LockGuard guard(g_policy_lock);
    if (!g_initialized) return Status::NotInitialized;
    if (cpu >= g_cpu_count) return Status::InvalidCpu;
    const size_t index = find_thread(id);
    if (index == kInvalidIndex) return Status::NotFound;

    ThreadStat& stat = g_threads[index].stat;
    if (stat.state != State::Running || stat.last_cpu != cpu) {
        return Status::InvalidArgument;
    }
    stat.state = State::Ready;
    return Status::Ok;
}

Status stat(ThreadId id, ThreadStat* out_stat) {
    sync::LockGuard guard(g_policy_lock);
    if (!g_initialized) return Status::NotInitialized;
    if (out_stat == nullptr) return Status::InvalidArgument;
    const size_t index = find_thread(id);
    if (index == kInvalidIndex) {
        *out_stat = {};
        return Status::NotFound;
    }
    *out_stat = g_threads[index].stat;
    return Status::Ok;
}

Status cpu_stat(size_t cpu, CpuStat* out_stat) {
    sync::LockGuard guard(g_policy_lock);
    if (!g_initialized) return Status::NotInitialized;
    if (out_stat == nullptr) return Status::InvalidArgument;
    if (cpu >= g_cpu_count) return Status::InvalidCpu;
    const RunQueue& queue = g_queues[cpu];
    CpuStat result{};
    result.cpu = cpu;
    result.queued = queue.count;
    result.dispatches = queue.dispatches;
    result.steals = queue.steals;
    for (size_t index = 0U; index < queue.count; ++index) {
        const size_t thread_index = find_thread(queue.ids[index]);
        if (thread_index != kInvalidIndex &&
            g_threads[thread_index].stat.state == State::Ready &&
            cpu_allowed(g_threads[thread_index].stat.affinity, cpu)) {
            ++result.ready;
        }
    }
    *out_stat = result;
    return Status::Ok;
}

size_t cpu_count() {
    sync::LockGuard guard(g_policy_lock);
    return g_initialized ? g_cpu_count : 0U;
}

CpuMask online_mask() {
    sync::LockGuard guard(g_policy_lock);
    return online_mask_unlocked();
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::NotInitialized: return "scheduler2 not initialized";
        case Status::AlreadyInitialized: return "scheduler2 already initialized";
        case Status::InvalidArgument: return "invalid scheduler2 argument";
        case Status::InvalidCpu: return "invalid scheduler2 cpu";
        case Status::InvalidAffinity: return "invalid scheduler2 affinity";
        case Status::AlreadyExists: return "scheduler2 thread already exists";
        case Status::NotFound: return "scheduler2 thread not found";
        case Status::CapacityReached: return "scheduler2 thread capacity reached";
        case Status::QueueFull: return "scheduler2 run queue full";
    }
    return "unknown scheduler2 status";
}

} // namespace threading::scheduler2

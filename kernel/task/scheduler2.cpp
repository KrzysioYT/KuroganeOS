#include "scheduler2.hpp"

namespace scheduler2 {
namespace {

void clear_bytes(void* destination, size_t size) {
    auto* bytes = static_cast<uint8_t*>(destination);
    for (size_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

CpuMask mask_for_count(size_t count) {
    if (count == 0U || count > MAX_CPUS) return 0U;
    return count == MAX_CPUS
        ? UINT64_MAX
        : ((UINT64_C(1) << count) - UINT64_C(1));
}

size_t find_runnable(const RunQueueSet& set, RunnableId id) {
    if (id == INVALID_RUNNABLE_ID) return MAX_RUNNABLES;
    for (size_t index = 0U; index < set.runnable_count; ++index) {
        if (set.runnables[index].queued && set.runnables[index].id == id) {
            return index;
        }
    }
    return MAX_RUNNABLES;
}

size_t choose_cpu(
    const RunQueueSet& set,
    CpuMask affinity,
    size_t preferred_cpu) {
    const CpuMask usable = affinity & online_mask(set);
    if (usable == 0U) return MAX_CPUS;

    size_t best = MAX_CPUS;
    size_t best_load = SIZE_MAX;

    if (preferred_cpu < set.online_cpu_count &&
        (usable & (UINT64_C(1) << preferred_cpu)) != 0U) {
        best = preferred_cpu;
        best_load = set.queues[preferred_cpu].count;
    }

    for (size_t step = 0U; step < set.online_cpu_count; ++step) {
        const size_t cpu =
            (set.assignment_cursor + step) % set.online_cpu_count;
        if ((usable & (UINT64_C(1) << cpu)) == 0U) continue;
        const size_t load = set.queues[cpu].count;
        if (best == MAX_CPUS || load < best_load) {
            best = cpu;
            best_load = load;
        }
    }
    return best;
}

bool erase_from_queue(
    RunQueueSet::Queue* queue,
    RunnableId id) {
    if (queue == nullptr) return false;
    for (size_t index = 0U; index < queue->count; ++index) {
        if (queue->entries[index] != id) continue;
        for (size_t move = index + 1U; move < queue->count; ++move) {
            queue->entries[move - 1U] = queue->entries[move];
        }
        --queue->count;
        if (queue->count < MAX_RUNNABLES) {
            queue->entries[queue->count] = INVALID_RUNNABLE_ID;
        }
        return true;
    }
    return false;
}

Status append_to_cpu(
    RunQueueSet* set,
    size_t runnable_index,
    size_t cpu) {
    if (set == nullptr || runnable_index >= MAX_RUNNABLES ||
        cpu >= set->online_cpu_count) {
        return Status::InvalidArgument;
    }
    auto& queue = set->queues[cpu];
    if (queue.count >= MAX_RUNNABLES) return Status::CapacityReached;
    const RunnableId id = set->runnables[runnable_index].id;
    queue.entries[queue.count++] = id;
    set->runnables[runnable_index].assigned_cpu = cpu;
    set->runnables[runnable_index].queued = true;
    set->assignment_cursor =
        set->online_cpu_count == 0U
            ? 0U
            : (cpu + 1U) % set->online_cpu_count;
    return Status::Ok;
}

void compact_runnables(RunQueueSet* set) {
    if (set == nullptr) return;
    size_t write = 0U;
    for (size_t read = 0U; read < set->runnable_count; ++read) {
        if (!set->runnables[read].queued) continue;
        if (write != read) set->runnables[write] = set->runnables[read];
        ++write;
    }
    for (size_t index = write; index < set->runnable_count; ++index) {
        set->runnables[index] = {};
    }
    set->runnable_count = write;
}

} // namespace

Status initialize(RunQueueSet* set, size_t online_cpu_count) {
    if (set == nullptr || online_cpu_count == 0U ||
        online_cpu_count > MAX_CPUS) {
        return Status::InvalidArgument;
    }
    if (set->initialized) return Status::AlreadyInitialized;
    clear_bytes(set, sizeof(*set));
    set->online_cpu_count = online_cpu_count;
    set->initialized = true;
    return Status::Ok;
}

CpuMask online_mask(const RunQueueSet& set) {
    return set.initialized ? mask_for_count(set.online_cpu_count) : 0U;
}

bool cpu_allowed(
    const RunQueueSet& set,
    CpuMask affinity,
    size_t cpu_index) {
    return set.initialized && cpu_index < set.online_cpu_count &&
        (affinity & (UINT64_C(1) << cpu_index)) != 0U;
}

Status enqueue(
    RunQueueSet* set,
    RunnableId id,
    CpuMask affinity,
    size_t preferred_cpu) {
    if (set == nullptr || !set->initialized) return Status::NotInitialized;
    if (id == INVALID_RUNNABLE_ID) return Status::InvalidArgument;
    if ((affinity & online_mask(*set)) == 0U) return Status::EmptyAffinity;
    if (find_runnable(*set, id) != MAX_RUNNABLES) {
        return Status::DuplicateRunnable;
    }
    if (set->runnable_count >= MAX_RUNNABLES) return Status::CapacityReached;

    const size_t cpu = choose_cpu(*set, affinity, preferred_cpu);
    if (cpu == MAX_CPUS) return Status::CpuOffline;

    const size_t index = set->runnable_count++;
    set->runnables[index] = {id, affinity, cpu, false};
    const Status status = append_to_cpu(set, index, cpu);
    if (status != Status::Ok) {
        set->runnables[index] = {};
        --set->runnable_count;
    }
    return status;
}

Status dequeue(
    RunQueueSet* set,
    size_t cpu_index,
    RunnableId* id) {
    if (id != nullptr) *id = INVALID_RUNNABLE_ID;
    if (set == nullptr || !set->initialized) return Status::NotInitialized;
    if (id == nullptr || cpu_index >= set->online_cpu_count) {
        return Status::InvalidArgument;
    }
    auto& queue = set->queues[cpu_index];
    if (queue.count == 0U) return Status::NotFound;

    const RunnableId selected = queue.entries[0U];
    if (!erase_from_queue(&queue, selected)) return Status::NotFound;
    const size_t index = find_runnable(*set, selected);
    if (index == MAX_RUNNABLES) return Status::NotFound;
    set->runnables[index].queued = false;
    *id = selected;
    compact_runnables(set);
    return Status::Ok;
}

Status remove(RunQueueSet* set, RunnableId id) {
    if (set == nullptr || !set->initialized) return Status::NotInitialized;
    const size_t index = find_runnable(*set, id);
    if (index == MAX_RUNNABLES) return Status::NotFound;
    const size_t cpu = set->runnables[index].assigned_cpu;
    if (cpu >= set->online_cpu_count ||
        !erase_from_queue(&set->queues[cpu], id)) {
        return Status::NotFound;
    }
    set->runnables[index].queued = false;
    compact_runnables(set);
    return Status::Ok;
}

Status migrate(
    RunQueueSet* set,
    RunnableId id,
    CpuMask new_affinity,
    size_t preferred_cpu) {
    if (set == nullptr || !set->initialized) return Status::NotInitialized;
    if ((new_affinity & online_mask(*set)) == 0U) {
        return Status::EmptyAffinity;
    }
    const size_t index = find_runnable(*set, id);
    if (index == MAX_RUNNABLES) return Status::NotFound;

    const size_t old_cpu = set->runnables[index].assigned_cpu;
    const size_t target = choose_cpu(*set, new_affinity, preferred_cpu);
    if (target == MAX_CPUS) return Status::CpuOffline;

    if (old_cpu == target) {
        set->runnables[index].affinity = new_affinity;
        return Status::Ok;
    }
    if (set->queues[target].count >= MAX_RUNNABLES) {
        return Status::CapacityReached;
    }
    if (old_cpu >= set->online_cpu_count ||
        !erase_from_queue(&set->queues[old_cpu], id)) {
        return Status::NotFound;
    }
    set->runnables[index].affinity = new_affinity;
    const Status status = append_to_cpu(set, index, target);
    if (status != Status::Ok) {
        // The target capacity was checked before removal. Reinsert on the old
        // CPU as a defensive rollback if a future append gains new failure
        // modes.
        set->runnables[index].affinity = UINT64_C(1) << old_cpu;
        static_cast<void>(append_to_cpu(set, index, old_cpu));
        return status;
    }
    return Status::Ok;
}

Status set_online_cpu_count(
    RunQueueSet* set,
    size_t online_cpu_count) {
    if (set == nullptr || !set->initialized) return Status::NotInitialized;
    if (online_cpu_count == 0U || online_cpu_count > MAX_CPUS) {
        return Status::InvalidArgument;
    }

    const CpuMask next_mask = mask_for_count(online_cpu_count);
    for (size_t index = 0U; index < set->runnable_count; ++index) {
        const auto& runnable = set->runnables[index];
        if (!runnable.queued) continue;
        if ((runnable.affinity & next_mask) == 0U) {
            return Status::EmptyAffinity;
        }
    }

    // Shrinking first migrates work away from CPUs that will become offline.
    if (online_cpu_count < set->online_cpu_count) {
        for (size_t cpu = online_cpu_count;
             cpu < set->online_cpu_count; ++cpu) {
            while (set->queues[cpu].count != 0U) {
                const RunnableId id = set->queues[cpu].entries[0U];
                const size_t index = find_runnable(*set, id);
                if (index == MAX_RUNNABLES) return Status::NotFound;
                const CpuMask affinity =
                    set->runnables[index].affinity & next_mask;

                size_t target = MAX_CPUS;
                size_t best_load = SIZE_MAX;
                for (size_t candidate = 0U;
                     candidate < online_cpu_count; ++candidate) {
                    if ((affinity & (UINT64_C(1) << candidate)) == 0U) continue;
                    if (set->queues[candidate].count < best_load) {
                        target = candidate;
                        best_load = set->queues[candidate].count;
                    }
                }
                if (target == MAX_CPUS) return Status::EmptyAffinity;
                if (!erase_from_queue(&set->queues[cpu], id)) {
                    return Status::NotFound;
                }
                if (set->queues[target].count >= MAX_RUNNABLES) {
                    return Status::CapacityReached;
                }
                set->queues[target].entries[set->queues[target].count++] = id;
                set->runnables[index].assigned_cpu = target;
            }
        }
    }

    set->online_cpu_count = online_cpu_count;
    set->assignment_cursor %= online_cpu_count;
    return Status::Ok;
}

bool queue_stat(
    const RunQueueSet& set,
    size_t cpu_index,
    QueueStat* stat) {
    if (!set.initialized || stat == nullptr ||
        cpu_index >= set.online_cpu_count) {
        return false;
    }
    *stat = {cpu_index, set.queues[cpu_index].count};
    return true;
}

bool runnable_stat(
    const RunQueueSet& set,
    RunnableId id,
    RunnableStat* stat) {
    if (!set.initialized || stat == nullptr) return false;
    const size_t index = find_runnable(set, id);
    if (index == MAX_RUNNABLES) return false;
    *stat = set.runnables[index];
    return true;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::NotInitialized: return "not initialized";
        case Status::AlreadyInitialized: return "already initialized";
        case Status::InvalidArgument: return "invalid argument";
        case Status::CapacityReached: return "capacity reached";
        case Status::DuplicateRunnable: return "runnable already queued";
        case Status::NotFound: return "runnable not found";
        case Status::CpuOffline: return "target CPU offline";
        case Status::EmptyAffinity: return "affinity has no online CPU";
    }
    return "unknown scheduler2 status";
}

} // namespace scheduler2

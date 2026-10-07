#pragma once

#include <stddef.h>
#include <stdint.h>

namespace scheduler2 {

using RunnableId = uint64_t;
using CpuMask = uint64_t;

constexpr RunnableId INVALID_RUNNABLE_ID = 0U;
constexpr size_t MAX_CPUS = 64U;
constexpr size_t MAX_RUNNABLES = 64U;

enum class Status : uint8_t {
    Ok = 0,
    NotInitialized,
    AlreadyInitialized,
    InvalidArgument,
    CapacityReached,
    DuplicateRunnable,
    NotFound,
    CpuOffline,
    EmptyAffinity,
};

struct QueueStat {
    size_t cpu_index;
    size_t runnable_count;
};

struct RunnableStat {
    RunnableId id;
    CpuMask affinity;
    size_t assigned_cpu;
    bool queued;
};

struct RunQueueSet {
    struct Queue {
        RunnableId entries[MAX_RUNNABLES];
        size_t count;
    };

    Queue queues[MAX_CPUS];
    RunnableStat runnables[MAX_RUNNABLES];
    size_t online_cpu_count;
    size_t runnable_count;
    size_t assignment_cursor;
    bool initialized;
};

Status initialize(RunQueueSet* set, size_t online_cpu_count);
CpuMask online_mask(const RunQueueSet& set);
bool cpu_allowed(const RunQueueSet& set, CpuMask affinity, size_t cpu_index);

Status enqueue(
    RunQueueSet* set,
    RunnableId id,
    CpuMask affinity,
    size_t preferred_cpu = SIZE_MAX);

Status dequeue(
    RunQueueSet* set,
    size_t cpu_index,
    RunnableId* id);

Status remove(RunQueueSet* set, RunnableId id);

Status migrate(
    RunQueueSet* set,
    RunnableId id,
    CpuMask new_affinity,
    size_t preferred_cpu = SIZE_MAX);

Status set_online_cpu_count(RunQueueSet* set, size_t online_cpu_count);

bool queue_stat(
    const RunQueueSet& set,
    size_t cpu_index,
    QueueStat* stat);

bool runnable_stat(
    const RunQueueSet& set,
    RunnableId id,
    RunnableStat* stat);

const char* status_message(Status status);

} // namespace scheduler2

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace threading::scheduler2 {

using ThreadId = uint64_t;
using CpuMask = uint64_t;

constexpr ThreadId INVALID_THREAD_ID = 0U;
constexpr size_t MAX_CPUS = 64U;
constexpr size_t MAX_SCHEDULABLE_THREADS = 64U;
constexpr uint8_t MAX_PRIORITY = 31U;

enum class Status : uint8_t {
    Ok = 0,
    NotInitialized,
    AlreadyInitialized,
    InvalidArgument,
    InvalidCpu,
    InvalidAffinity,
    AlreadyExists,
    NotFound,
    CapacityReached,
    QueueFull
};

enum class State : uint8_t {
    Ready = 0,
    Running,
    Blocked,
    Sleeping,
    Terminated
};

struct ThreadStat {
    ThreadId id;
    State state;
    uint8_t priority;
    CpuMask affinity;
    size_t home_cpu;
    size_t last_cpu;
    uint64_t dispatches;
    uint64_t migrations;
};

struct CpuStat {
    size_t cpu;
    size_t queued;
    size_t ready;
    uint64_t dispatches;
    uint64_t steals;
};

Status initialize(size_t online_cpus);
Status reset(size_t online_cpus);
// Grow the topology after SMP discovery without losing pre-existing BSP
// threads, queue order or accounting.
Status expand_cpu_count(size_t online_cpus);

Status register_thread(
    ThreadId id,
    uint8_t priority,
    CpuMask affinity,
    size_t preferred_cpu = MAX_CPUS);
Status unregister_thread(ThreadId id);

Status set_state(ThreadId id, State state);
Status set_affinity(ThreadId id, CpuMask affinity);
Status set_priority(ThreadId id, uint8_t priority);

// Selects the highest-priority ready thread from the local run queue. Equal
// priorities are round-robin. If the local queue has no eligible thread, one
// eligible ready thread may be stolen from another CPU and migrated locally.
// Selection does not change the thread state; the caller owns Ready->Running.
Status pick_next(
    size_t cpu,
    ThreadId excluded,
    ThreadId* out_id);

// Roll back a Ready->Running reservation made by pick_next when the execution
// layer cannot commit the dispatch. The reservation is released only by the
// CPU that owns the most recent dispatch, preventing a stale CPU from making a
// thread runnable while it is already executing elsewhere.
Status cancel_dispatch(ThreadId id, size_t cpu);

Status stat(ThreadId id, ThreadStat* out_stat);
Status cpu_stat(size_t cpu, CpuStat* out_stat);

size_t cpu_count();
CpuMask online_mask();
const char* status_message(Status status);

} // namespace threading::scheduler2

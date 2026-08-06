#pragma once

#include "../net/network.hpp"
#include "../task/scheduler.hpp"

#include <stddef.h>
#include <stdint.h>

namespace diagnostics::profiler {

static constexpr uint32_t SNAPSHOT_VERSION = 1;
static constexpr size_t MAX_ACTIVE_APPLICATION_NAME_LENGTH = 32;

// Capabilities describe measurements, not merely compiled code. A capability
// appears in Snapshot::available_capabilities only when its source was
// successfully read for that snapshot.
static constexpr uint64_t CAPABILITY_SCHEDULER_COUNTERS = UINT64_C(1) << 0;
static constexpr uint64_t CAPABILITY_HEAP_STATE = UINT64_C(1) << 1;
static constexpr uint64_t CAPABILITY_PHYSICAL_MEMORY_STATE = UINT64_C(1) << 2;
static constexpr uint64_t CAPABILITY_NETWORK_COUNTERS = UINT64_C(1) << 3;
static constexpr uint64_t CAPABILITY_APPLICATION_STATE = UINT64_C(1) << 4;

static constexpr uint64_t CAPABILITY_CPU_USAGE = UINT64_C(1) << 16;
static constexpr uint64_t CAPABILITY_FUNCTION_TIMING = UINT64_C(1) << 17;
static constexpr uint64_t CAPABILITY_STACK_SAMPLING = UINT64_C(1) << 18;
static constexpr uint64_t CAPABILITY_ALLOCATION_EVENTS = UINT64_C(1) << 19;
static constexpr uint64_t CAPABILITY_LEAK_DETECTION = UINT64_C(1) << 20;
static constexpr uint64_t CAPABILITY_IPC_EVENTS = UINT64_C(1) << 21;
static constexpr uint64_t CAPABILITY_SYSCALL_EVENTS = UINT64_C(1) << 22;
static constexpr uint64_t CAPABILITY_FILE_EVENTS = UINT64_C(1) << 23;
static constexpr uint64_t CAPABILITY_GUI_EVENTS = UINT64_C(1) << 24;

static constexpr uint64_t IMPLEMENTED_CAPABILITIES =
    CAPABILITY_SCHEDULER_COUNTERS |
    CAPABILITY_HEAP_STATE |
    CAPABILITY_PHYSICAL_MEMORY_STATE |
    CAPABILITY_NETWORK_COUNTERS |
    CAPABILITY_APPLICATION_STATE;

static constexpr uint64_t KNOWN_UNIMPLEMENTED_CAPABILITIES =
    CAPABILITY_CPU_USAGE |
    CAPABILITY_FUNCTION_TIMING |
    CAPABILITY_STACK_SAMPLING |
    CAPABILITY_ALLOCATION_EVENTS |
    CAPABILITY_LEAK_DETECTION |
    CAPABILITY_IPC_EVENTS |
    CAPABILITY_SYSCALL_EVENTS |
    CAPABILITY_FILE_EVENTS |
    CAPABILITY_GUI_EVENTS;

static constexpr uint32_t SOURCE_SCHEDULER = UINT32_C(1) << 0;
static constexpr uint32_t SOURCE_HEAP = UINT32_C(1) << 1;
static constexpr uint32_t SOURCE_PHYSICAL_MEMORY = UINT32_C(1) << 2;
static constexpr uint32_t SOURCE_NETWORK = UINT32_C(1) << 3;
static constexpr uint32_t SOURCE_APPLICATIONS = UINT32_C(1) << 4;
static constexpr uint32_t ALL_SOURCES =
    SOURCE_SCHEDULER |
    SOURCE_HEAP |
    SOURCE_PHYSICAL_MEMORY |
    SOURCE_NETWORK |
    SOURCE_APPLICATIONS;

enum class Status : uint8_t {
    Ok = 0,
    Partial,
    Disabled,
    InvalidArgument
};

struct SchedulerSnapshot {
    scheduler::SchedulerMetrics metrics;
    size_t task_count;
    size_t waiting_tasks;
    size_t ready_tasks;
    size_t running_tasks;
    size_t suspended_tasks;
};

struct HeapSnapshot {
    size_t total_bytes;
    size_t used_bytes;
    size_t free_bytes;
    size_t live_allocations;
};

struct PhysicalMemorySnapshot {
    uintptr_t base;
    size_t frame_size;
    size_t total_frames;
    size_t used_frames;
    size_t free_frames;
    size_t reserved_frames;
};

struct NetworkSnapshot {
    net::NetworkStats stats;
};

struct ApplicationSnapshot {
    size_t registered_applications;
    bool running;
    char active_name[MAX_ACTIVE_APPLICATION_NAME_LENGTH + 1];
};

struct Snapshot {
    uint32_t size;
    uint32_t version;
    uint64_t sequence;
    scheduler::Tick scheduler_tick;
    uint64_t implemented_capabilities;
    uint64_t available_capabilities;
    uint32_t available_sources;
    uint32_t unavailable_sources;
    SchedulerSnapshot scheduler;
    HeapSnapshot heap;
    PhysicalMemorySnapshot physical_memory;
    NetworkSnapshot network;
    ApplicationSnapshot applications;
};

// The profiler starts disabled. A disabled capture performs no source reads and
// leaves the caller's output untouched.
void set_enabled(bool enabled);
bool is_enabled();

// Resets only the monotonically increasing profiler snapshot sequence. It does
// not reset counters owned by scheduler, memory, network, or applications.
void reset_sequence();

Status capture(Snapshot* output);
uint64_t implemented_capabilities();
uint64_t known_unimplemented_capabilities();
const char* status_message(Status status);

} // namespace diagnostics::profiler

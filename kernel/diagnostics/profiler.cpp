#include "profiler.hpp"

#include "../apps/framework.hpp"
#include "../memory/allocator.hpp"
#include "../memory/physical_memory.hpp"
#include "../net/service.hpp"

namespace diagnostics::profiler {

namespace {

static uint8_t g_enabled = 0;
static uint64_t g_sequence = 0;

struct SchedulerCollector {
    SchedulerSnapshot* snapshot;
};

bool collect_task(const scheduler::TaskStat* task, void* context) {
    if (!task || !context) {
        return false;
    }

    SchedulerCollector* collector =
        static_cast<SchedulerCollector*>(context);
    SchedulerSnapshot& snapshot = *collector->snapshot;
    ++snapshot.task_count;

    switch (task->state) {
        case scheduler::TaskState::Waiting:
            ++snapshot.waiting_tasks;
            break;
        case scheduler::TaskState::Ready:
            ++snapshot.ready_tasks;
            break;
        case scheduler::TaskState::Running:
            ++snapshot.running_tasks;
            break;
        case scheduler::TaskState::Suspended:
            ++snapshot.suspended_tasks;
            break;
        case scheduler::TaskState::Empty:
            break;
    }
    return true;
}

bool count_application(const applications::Definition&, void* context) {
    if (!context) {
        return false;
    }
    size_t* count = static_cast<size_t*>(context);
    ++(*count);
    return true;
}

void copy_active_name(char* output, const char* name) {
    size_t index = 0;
    if (name) {
        while (index < MAX_ACTIVE_APPLICATION_NAME_LENGTH &&
               name[index] != '\0') {
            output[index] = name[index];
            ++index;
        }
    }
    output[index] = '\0';
    for (++index; index <= MAX_ACTIVE_APPLICATION_NAME_LENGTH; ++index) {
        output[index] = '\0';
    }
}

uint64_t next_sequence() {
    uint64_t current = __atomic_load_n(&g_sequence, __ATOMIC_RELAXED);
    while (current != UINT64_MAX) {
        const uint64_t next = current + 1;
        if (__atomic_compare_exchange_n(
                &g_sequence,
                &current,
                next,
                true,
                __ATOMIC_RELAXED,
                __ATOMIC_RELAXED)) {
            return next;
        }
    }
    return UINT64_MAX;
}

} // namespace

void set_enabled(bool enabled) {
    __atomic_store_n(
        &g_enabled,
        enabled ? static_cast<uint8_t>(1) : static_cast<uint8_t>(0),
        __ATOMIC_RELEASE);
}

bool is_enabled() {
    return __atomic_load_n(&g_enabled, __ATOMIC_ACQUIRE) != 0;
}

void reset_sequence() {
    __atomic_store_n(&g_sequence, UINT64_C(0), __ATOMIC_RELEASE);
}

Status capture(Snapshot* output) {
    if (!output) {
        return Status::InvalidArgument;
    }
    if (!is_enabled()) {
        return Status::Disabled;
    }

    Snapshot snapshot = {};
    snapshot.size = sizeof(Snapshot);
    snapshot.version = SNAPSHOT_VERSION;
    snapshot.implemented_capabilities = IMPLEMENTED_CAPABILITIES;

    SchedulerCollector scheduler_collector = {&snapshot.scheduler};
    if (scheduler::get_metrics(&snapshot.scheduler.metrics) ==
            scheduler::Status::Ok &&
        scheduler::list(collect_task, &scheduler_collector) ==
            scheduler::Status::Ok) {
        snapshot.scheduler_tick = scheduler::now();
        snapshot.available_sources |= SOURCE_SCHEDULER;
        snapshot.available_capabilities |= CAPABILITY_SCHEDULER_COUNTERS;
    }

    if (memory::kernel_heap_initialized()) {
        snapshot.heap.total_bytes = memory::total_bytes();
        snapshot.heap.used_bytes = memory::used_bytes();
        snapshot.heap.free_bytes = memory::free_bytes();
        snapshot.heap.live_allocations = memory::allocation_count();
        snapshot.available_sources |= SOURCE_HEAP;
        snapshot.available_capabilities |= CAPABILITY_HEAP_STATE;
    }

    if (memory::physical_memory_initialized()) {
        snapshot.physical_memory.base = memory::physical_memory_base();
        snapshot.physical_memory.frame_size = memory::physical_frame_size();
        snapshot.physical_memory.total_frames = memory::total_frames();
        snapshot.physical_memory.used_frames = memory::used_frames();
        snapshot.physical_memory.free_frames = memory::free_frames();
        snapshot.physical_memory.reserved_frames = memory::reserved_frames();
        snapshot.available_sources |= SOURCE_PHYSICAL_MEMORY;
        snapshot.available_capabilities |=
            CAPABILITY_PHYSICAL_MEMORY_STATE;
    }

    if (net::service::ready() &&
        net::service::stats(&snapshot.network.stats) == net::Status::Ok) {
        snapshot.available_sources |= SOURCE_NETWORK;
        snapshot.available_capabilities |= CAPABILITY_NETWORK_COUNTERS;
    }

    applications::list(
        count_application,
        &snapshot.applications.registered_applications);
    snapshot.applications.running = applications::running();
    copy_active_name(
        snapshot.applications.active_name,
        applications::active_name());
    snapshot.available_sources |= SOURCE_APPLICATIONS;
    snapshot.available_capabilities |= CAPABILITY_APPLICATION_STATE;

    snapshot.unavailable_sources =
        ALL_SOURCES & ~snapshot.available_sources;
    snapshot.sequence = next_sequence();
    *output = snapshot;

    return snapshot.unavailable_sources == 0
        ? Status::Ok
        : Status::Partial;
}

uint64_t implemented_capabilities() {
    return IMPLEMENTED_CAPABILITIES;
}

uint64_t known_unimplemented_capabilities() {
    return KNOWN_UNIMPLEMENTED_CAPABILITIES;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok:
            return "ok";
        case Status::Partial:
            return "partial snapshot";
        case Status::Disabled:
            return "profiler disabled";
        case Status::InvalidArgument:
            return "invalid argument";
    }
    return "unknown status";
}

} // namespace diagnostics::profiler

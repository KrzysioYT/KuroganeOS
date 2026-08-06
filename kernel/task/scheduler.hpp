#pragma once

#include <stddef.h>
#include <stdint.h>

namespace scheduler {

typedef uint64_t Tick;
typedef uint64_t TaskId;
typedef void (*TaskCallback)(void* context);

static constexpr size_t MAX_TASKS = 32;
static constexpr size_t MAX_TASK_NAME_LENGTH = 31;
static constexpr Tick MAX_FORWARD_TICK_DELTA = UINT64_C(0x7fffffffffffffff);
static constexpr TaskId INVALID_TASK_ID = 0;

enum class Status : uint8_t {
    Ok = 0,
    NotInitialized,
    AlreadyInitialized,
    InvalidArgument,
    NameTooLong,
    PeriodTooLarge,
    CapacityReached,
    NotFound,
    InvalidState,
    AlreadySuspended,
    NotSuspended,
    NotRunning,
    CancellationPending,
    ClockRegression,
    Busy,
    ReentrantCall,
    BudgetExhausted,
    IterationStopped
};

enum class TaskState : uint8_t {
    Empty = 0,
    Waiting,
    Ready,
    Running,
    Suspended
};

struct TaskStat {
    TaskId id;
    char name[MAX_TASK_NAME_LENGTH + 1];
    TaskState state;
    Tick period_ticks;
    Tick next_release;
    uint64_t run_count;
    uint64_t release_count;
    uint64_t coalesced_release_count;
    uint64_t yield_count;
    Tick last_run_tick;
    bool pending;
    bool cancellation_pending;
    bool suspension_pending;
};

struct SchedulerMetrics {
    uint64_t tick_calls;
    uint64_t rejected_tick_calls;
    uint64_t tasks_created;
    uint64_t tasks_completed;
    uint64_t tasks_cancelled;
    uint64_t releases;
    uint64_t coalesced_releases;
    uint64_t callbacks_executed;
    uint64_t yield_requests;
    uint64_t suspensions;
    uint64_t resumptions;
    uint64_t budget_exhaustions;
};

struct RunResult {
    size_t executed;
    size_t ready_remaining;
};

// A false return stops a stable-snapshot iteration.
typedef bool (*ListCallback)(const TaskStat* task, void* context);

Status initialize(Tick initial_now = 0);
Status reset(Tick initial_now = 0);

// period_ticks == 0 creates a one-shot task ready for immediate dispatch.
Status create(
    const char* name,
    TaskCallback callback,
    void* context,
    Tick period_ticks,
    TaskId* out_id
);

Status cancel(TaskId id);
Status suspend(TaskId id);
Status resume(TaskId id);

// IRQ-safe scheduling edge: records time and readiness, never invokes callbacks.
Status tick(Tick now);

// Executes at most budget callbacks in task context. A yielded callback must
// return; yield() requests another dispatch instead of switching its stack.
Status run_pending(size_t budget, RunResult* out_result = nullptr);
Status yield();

Status stat(TaskId id, TaskStat* out_stat);
Status list(ListCallback callback, void* context);
Status get_metrics(SchedulerMetrics* out_metrics);

TaskId current_task();
Tick now();
const char* status_message(Status status);

} // namespace scheduler

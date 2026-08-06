#include "scheduler.hpp"

namespace scheduler {

namespace {

static constexpr uint64_t TASK_ID_SLOT_MASK = UINT64_C(0xff);
static constexpr uint64_t TASK_ID_MAX_GENERATION = UINT64_MAX >> 8;
static constexpr size_t INVALID_SLOT = static_cast<size_t>(-1);

struct TaskSlot {
    uint8_t state;
    uint8_t pending;
    uint8_t cancel_requested;
    uint8_t suspend_requested;
    uint8_t yield_requested;
    TaskId id;
    uint64_t generation;
    char name[MAX_TASK_NAME_LENGTH + 1];
    TaskCallback callback;
    void* context;
    Tick period_ticks;
    Tick next_release;
    uint64_t run_count;
    uint64_t release_count;
    uint64_t coalesced_release_count;
    uint64_t yield_count;
    Tick last_run_tick;
};

static TaskSlot g_tasks[MAX_TASKS] = {};
static SchedulerMetrics g_metrics = {};
static uint8_t g_initialized = 0;
static uint8_t g_run_active = 0;
static Tick g_now = 0;
static size_t g_dispatch_cursor = 0;
static size_t g_current_slot = INVALID_SLOT;

uint8_t atomic_load_u8(const uint8_t* value) {
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

void atomic_store_u8(uint8_t* target, uint8_t value) {
    __atomic_store_n(target, value, __ATOMIC_RELEASE);
}

uint8_t atomic_exchange_u8(uint8_t* target, uint8_t value) {
    return __atomic_exchange_n(target, value, __ATOMIC_ACQ_REL);
}

uint64_t atomic_load_u64(const uint64_t* value) {
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

void atomic_store_u64(uint64_t* target, uint64_t value) {
    __atomic_store_n(target, value, __ATOMIC_RELEASE);
}

bool atomic_compare_u8(uint8_t* target, uint8_t expected, uint8_t desired) {
    return __atomic_compare_exchange_n(
        target,
        &expected,
        desired,
        false,
        __ATOMIC_ACQ_REL,
        __ATOMIC_ACQUIRE
    );
}

bool atomic_compare_u64(uint64_t* target, uint64_t expected, uint64_t desired) {
    return __atomic_compare_exchange_n(
        target,
        &expected,
        desired,
        false,
        __ATOMIC_ACQ_REL,
        __ATOMIC_ACQUIRE
    );
}

void saturating_add(uint64_t* target, uint64_t increment) {
    if (increment == 0) {
        return;
    }

    uint64_t current = __atomic_load_n(target, __ATOMIC_RELAXED);
    while (true) {
        const uint64_t next =
            UINT64_MAX - current < increment ? UINT64_MAX : current + increment;
        if (__atomic_compare_exchange_n(
                target,
                &current,
                next,
                true,
                __ATOMIC_RELAXED,
                __ATOMIC_RELAXED
            )) {
            return;
        }
    }
}

void clear_bytes(void* destination, size_t size) {
    unsigned char* bytes = static_cast<unsigned char*>(destination);
    for (size_t i = 0; i < size; ++i) {
        bytes[i] = 0;
    }
}

TaskState load_state(const TaskSlot& slot) {
    return static_cast<TaskState>(atomic_load_u8(&slot.state));
}

void store_state(TaskSlot& slot, TaskState state) {
    atomic_store_u8(&slot.state, static_cast<uint8_t>(state));
}

bool compare_state(TaskSlot& slot, TaskState expected, TaskState desired) {
    return atomic_compare_u8(
        &slot.state,
        static_cast<uint8_t>(expected),
        static_cast<uint8_t>(desired)
    );
}

void clear_slot_after_empty(TaskSlot& slot) {
    const uint64_t generation = slot.generation;
    clear_bytes(&slot, sizeof(slot));
    slot.generation = generation;
    store_state(slot, TaskState::Empty);
}

void retire_slot(size_t index, bool completed, bool cancelled) {
    TaskSlot& slot = g_tasks[index];
    store_state(slot, TaskState::Empty);
    clear_slot_after_empty(slot);
    if (completed) {
        saturating_add(&g_metrics.tasks_completed, 1);
    }
    if (cancelled) {
        saturating_add(&g_metrics.tasks_cancelled, 1);
    }
}

Status validate_name(const char* name, size_t* out_length) {
    if (!name || !out_length) {
        return Status::InvalidArgument;
    }

    for (size_t i = 0; i <= MAX_TASK_NAME_LENGTH; ++i) {
        if (name[i] == '\0') {
            if (i == 0) {
                return Status::InvalidArgument;
            }
            *out_length = i;
            return Status::Ok;
        }
    }
    return Status::NameTooLong;
}

TaskId make_task_id(TaskSlot& slot, size_t index) {
    if (slot.generation == 0 || slot.generation >= TASK_ID_MAX_GENERATION) {
        slot.generation = 1;
    } else {
        ++slot.generation;
    }
    return (slot.generation << 8) | static_cast<TaskId>(index + 1);
}

bool decode_task_id(TaskId id, size_t* out_index) {
    if (id == INVALID_TASK_ID || !out_index) {
        return false;
    }

    const uint64_t encoded_slot = id & TASK_ID_SLOT_MASK;
    if (encoded_slot == 0 || encoded_slot > MAX_TASKS) {
        return false;
    }
    *out_index = static_cast<size_t>(encoded_slot - 1);
    return true;
}

Status find_task(TaskId id, size_t* out_index) {
    size_t index = 0;
    if (!decode_task_id(id, &index)) {
        return Status::NotFound;
    }

    TaskSlot& slot = g_tasks[index];
    if (load_state(slot) == TaskState::Empty || atomic_load_u64(&slot.id) != id) {
        return Status::NotFound;
    }
    *out_index = index;
    return Status::Ok;
}

bool time_reached(Tick current, Tick deadline) {
    return current - deadline <= MAX_FORWARD_TICK_DELTA;
}

void mark_due_releases(TaskSlot& slot, Tick current) {
    const TaskState state = load_state(slot);
    if (state == TaskState::Empty || state == TaskState::Suspended) {
        return;
    }

    const Tick period = atomic_load_u64(&slot.period_ticks);
    if (period == 0) {
        return;
    }

    Tick deadline = 0;
    uint64_t release_count = 0;
    while (true) {
        deadline = atomic_load_u64(&slot.next_release);
        if (!time_reached(current, deadline)) {
            return;
        }

        const Tick elapsed = current - deadline;
        release_count = elapsed / period + 1;
        if (atomic_compare_u64(
                &slot.next_release,
                deadline,
                deadline + release_count * period
            )) {
            break;
        }
    }

    const uint8_t was_pending = atomic_exchange_u8(&slot.pending, 1);
    const uint64_t coalesced =
        release_count - (was_pending == 0 ? UINT64_C(1) : UINT64_C(0));
    saturating_add(&slot.release_count, release_count);
    saturating_add(&slot.coalesced_release_count, coalesced);
    saturating_add(&g_metrics.releases, release_count);
    saturating_add(&g_metrics.coalesced_releases, coalesced);

    if (state == TaskState::Waiting) {
        compare_state(slot, TaskState::Waiting, TaskState::Ready);
    }
}

void promote_waiting_if_pending(TaskSlot& slot) {
    if (atomic_load_u8(&slot.pending) != 0) {
        compare_state(slot, TaskState::Waiting, TaskState::Ready);
    }
}

size_t count_ready_tasks() {
    size_t ready = 0;
    for (size_t i = 0; i < MAX_TASKS; ++i) {
        if (load_state(g_tasks[i]) == TaskState::Ready) {
            ++ready;
        }
    }
    return ready;
}

bool claim_next_ready(size_t* out_index) {
    for (size_t offset = 0; offset < MAX_TASKS; ++offset) {
        const size_t index = (g_dispatch_cursor + offset) % MAX_TASKS;
        TaskSlot& slot = g_tasks[index];
        if (load_state(slot) != TaskState::Ready) {
            continue;
        }

        const uint8_t consumed_pending = atomic_exchange_u8(&slot.pending, 0);
        if (compare_state(slot, TaskState::Ready, TaskState::Running)) {
            g_dispatch_cursor = (index + 1) % MAX_TASKS;
            *out_index = index;
            return true;
        }

        if (consumed_pending != 0) {
            atomic_store_u8(&slot.pending, 1);
        }
    }
    return false;
}

void copy_task_stat(size_t index, TaskStat* out_stat) {
    TaskSlot& slot = g_tasks[index];
    out_stat->id = atomic_load_u64(&slot.id);
    for (size_t i = 0; i <= MAX_TASK_NAME_LENGTH; ++i) {
        out_stat->name[i] = slot.name[i];
        if (slot.name[i] == '\0') {
            for (size_t j = i + 1; j <= MAX_TASK_NAME_LENGTH; ++j) {
                out_stat->name[j] = '\0';
            }
            break;
        }
    }
    out_stat->state = load_state(slot);
    out_stat->period_ticks = atomic_load_u64(&slot.period_ticks);
    out_stat->next_release = atomic_load_u64(&slot.next_release);
    out_stat->run_count = atomic_load_u64(&slot.run_count);
    out_stat->release_count = atomic_load_u64(&slot.release_count);
    out_stat->coalesced_release_count =
        atomic_load_u64(&slot.coalesced_release_count);
    out_stat->yield_count = atomic_load_u64(&slot.yield_count);
    out_stat->last_run_tick = atomic_load_u64(&slot.last_run_tick);
    out_stat->pending = atomic_load_u8(&slot.pending) != 0;
    out_stat->cancellation_pending =
        atomic_load_u8(&slot.cancel_requested) != 0;
    out_stat->suspension_pending =
        atomic_load_u8(&slot.suspend_requested) != 0;
}

void zero_metrics() {
    clear_bytes(&g_metrics, sizeof(g_metrics));
}

} // namespace

Status initialize(Tick initial_now) {
    if (atomic_load_u8(&g_initialized) != 0) {
        return Status::AlreadyInitialized;
    }

    for (size_t i = 0; i < MAX_TASKS; ++i) {
        clear_slot_after_empty(g_tasks[i]);
    }
    zero_metrics();
    atomic_store_u64(&g_now, initial_now);
    g_dispatch_cursor = 0;
    g_current_slot = INVALID_SLOT;
    atomic_store_u8(&g_run_active, 0);
    atomic_store_u8(&g_initialized, 1);
    return Status::Ok;
}

Status reset(Tick initial_now) {
    if (atomic_load_u8(&g_initialized) == 0) {
        return Status::NotInitialized;
    }
    if (atomic_load_u8(&g_run_active) != 0) {
        return Status::Busy;
    }

    atomic_store_u8(&g_initialized, 0);
    for (size_t i = 0; i < MAX_TASKS; ++i) {
        store_state(g_tasks[i], TaskState::Empty);
        clear_slot_after_empty(g_tasks[i]);
    }
    zero_metrics();
    atomic_store_u64(&g_now, initial_now);
    g_dispatch_cursor = 0;
    g_current_slot = INVALID_SLOT;
    atomic_store_u8(&g_initialized, 1);
    return Status::Ok;
}

Status create(
    const char* name,
    TaskCallback callback,
    void* context,
    Tick period_ticks,
    TaskId* out_id
) {
    if (out_id) {
        *out_id = INVALID_TASK_ID;
    }
    if (atomic_load_u8(&g_initialized) == 0) {
        return Status::NotInitialized;
    }
    if (!callback || !out_id) {
        return Status::InvalidArgument;
    }
    if (period_ticks > MAX_FORWARD_TICK_DELTA) {
        return Status::PeriodTooLarge;
    }

    size_t name_length = 0;
    Status status = validate_name(name, &name_length);
    if (status != Status::Ok) {
        return status;
    }

    size_t free_index = INVALID_SLOT;
    for (size_t i = 0; i < MAX_TASKS; ++i) {
        if (load_state(g_tasks[i]) == TaskState::Empty) {
            free_index = i;
            break;
        }
    }
    if (free_index == INVALID_SLOT) {
        return Status::CapacityReached;
    }

    TaskSlot& slot = g_tasks[free_index];
    const uint64_t old_generation = slot.generation;
    clear_bytes(&slot, sizeof(slot));
    slot.generation = old_generation;

    const TaskId id = make_task_id(slot, free_index);
    slot.id = id;
    for (size_t i = 0; i < name_length; ++i) {
        slot.name[i] = name[i];
    }
    slot.name[name_length] = '\0';
    slot.callback = callback;
    slot.context = context;
    slot.period_ticks = period_ticks;

    const Tick current = atomic_load_u64(&g_now);
    if (period_ticks == 0) {
        atomic_store_u8(&slot.pending, 1);
        slot.release_count = 1;
        store_state(slot, TaskState::Ready);
        saturating_add(&g_metrics.releases, 1);
    } else {
        slot.next_release = current + period_ticks;
        atomic_store_u8(&slot.pending, 0);
        store_state(slot, TaskState::Waiting);
        mark_due_releases(slot, atomic_load_u64(&g_now));
    }

    saturating_add(&g_metrics.tasks_created, 1);
    *out_id = id;
    return Status::Ok;
}

Status cancel(TaskId id) {
    if (atomic_load_u8(&g_initialized) == 0) {
        return Status::NotInitialized;
    }

    while (true) {
        size_t index = 0;
        Status status = find_task(id, &index);
        if (status != Status::Ok) {
            return status;
        }

        TaskSlot& slot = g_tasks[index];
        const TaskState state = load_state(slot);
        if (state == TaskState::Running) {
            if (g_current_slot != index || atomic_load_u64(&slot.id) != id) {
                return Status::Busy;
            }
            if (atomic_exchange_u8(&slot.cancel_requested, 1) != 0) {
                return Status::CancellationPending;
            }
            return Status::Ok;
        }
        if (state == TaskState::Empty) {
            return Status::NotFound;
        }

        if (compare_state(slot, state, TaskState::Empty)) {
            clear_slot_after_empty(slot);
            saturating_add(&g_metrics.tasks_cancelled, 1);
            return Status::Ok;
        }
    }
}

Status suspend(TaskId id) {
    if (atomic_load_u8(&g_initialized) == 0) {
        return Status::NotInitialized;
    }

    while (true) {
        size_t index = 0;
        Status status = find_task(id, &index);
        if (status != Status::Ok) {
            return status;
        }

        TaskSlot& slot = g_tasks[index];
        const TaskState state = load_state(slot);
        if (state == TaskState::Suspended) {
            return Status::AlreadySuspended;
        }
        if (state == TaskState::Running) {
            if (g_current_slot != index || atomic_load_u64(&slot.id) != id) {
                return Status::Busy;
            }
            if (atomic_exchange_u8(&slot.suspend_requested, 1) != 0) {
                return Status::AlreadySuspended;
            }
            return Status::Ok;
        }
        if (state != TaskState::Ready && state != TaskState::Waiting) {
            return Status::InvalidState;
        }

        if (compare_state(slot, state, TaskState::Suspended)) {
            saturating_add(&g_metrics.suspensions, 1);
            return Status::Ok;
        }
    }
}

Status resume(TaskId id) {
    if (atomic_load_u8(&g_initialized) == 0) {
        return Status::NotInitialized;
    }

    size_t index = 0;
    Status status = find_task(id, &index);
    if (status != Status::Ok) {
        return status;
    }

    TaskSlot& slot = g_tasks[index];
    const TaskState state = load_state(slot);
    if (state == TaskState::Running) {
        if (g_current_slot != index || atomic_load_u64(&slot.id) != id) {
            return Status::Busy;
        }
        if (atomic_exchange_u8(&slot.suspend_requested, 0) == 0) {
            return Status::NotSuspended;
        }
        return Status::Ok;
    }
    if (state != TaskState::Suspended) {
        return Status::NotSuspended;
    }

    const Tick period = atomic_load_u64(&slot.period_ticks);
    atomic_store_u64(&slot.next_release, atomic_load_u64(&g_now) + period);
    if (period == 0) {
        atomic_store_u8(&slot.pending, 1);
    }

    const TaskState resumed_state =
        atomic_load_u8(&slot.pending) != 0 ? TaskState::Ready : TaskState::Waiting;
    if (!compare_state(slot, TaskState::Suspended, resumed_state)) {
        return Status::InvalidState;
    }
    if (resumed_state == TaskState::Waiting) {
        mark_due_releases(slot, atomic_load_u64(&g_now));
    }

    saturating_add(&g_metrics.resumptions, 1);
    return Status::Ok;
}

Status tick(Tick current) {
    if (atomic_load_u8(&g_initialized) == 0) {
        return Status::NotInitialized;
    }

    const Tick previous = atomic_load_u64(&g_now);
    const Tick forward_delta = current - previous;
    if (forward_delta > MAX_FORWARD_TICK_DELTA) {
        saturating_add(&g_metrics.rejected_tick_calls, 1);
        return Status::ClockRegression;
    }

    atomic_store_u64(&g_now, current);
    saturating_add(&g_metrics.tick_calls, 1);

    for (size_t i = 0; i < MAX_TASKS; ++i) {
        mark_due_releases(g_tasks[i], current);
    }

    return Status::Ok;
}

Status run_pending(size_t budget, RunResult* out_result) {
    if (out_result) {
        out_result->executed = 0;
        out_result->ready_remaining = 0;
    }
    if (atomic_load_u8(&g_initialized) == 0) {
        return Status::NotInitialized;
    }
    if (atomic_exchange_u8(&g_run_active, 1) != 0) {
        return Status::ReentrantCall;
    }

    size_t executed = 0;
    while (executed < budget) {
        size_t index = INVALID_SLOT;
        if (!claim_next_ready(&index)) {
            break;
        }

        TaskSlot& slot = g_tasks[index];
        atomic_store_u8(&slot.cancel_requested, 0);
        atomic_store_u8(&slot.suspend_requested, 0);
        atomic_store_u8(&slot.yield_requested, 0);
        atomic_store_u64(&slot.last_run_tick, atomic_load_u64(&g_now));
        saturating_add(&slot.run_count, 1);
        saturating_add(&g_metrics.callbacks_executed, 1);

        g_current_slot = index;
        TaskCallback callback = slot.callback;
        void* context = slot.context;
        if (callback) {
            callback(context);
        }
        g_current_slot = INVALID_SLOT;
        ++executed;

        const bool cancel_requested =
            atomic_exchange_u8(&slot.cancel_requested, 0) != 0;
        const bool suspend_requested =
            atomic_exchange_u8(&slot.suspend_requested, 0) != 0;
        const bool yield_requested =
            atomic_exchange_u8(&slot.yield_requested, 0) != 0;

        if (cancel_requested) {
            retire_slot(index, false, true);
            continue;
        }
        if (yield_requested) {
            atomic_store_u8(&slot.pending, 1);
        }
        if (suspend_requested) {
            store_state(slot, TaskState::Suspended);
            saturating_add(&g_metrics.suspensions, 1);
            continue;
        }

        const Tick period = atomic_load_u64(&slot.period_ticks);
        if (period == 0 && atomic_load_u8(&slot.pending) == 0) {
            retire_slot(index, true, false);
            continue;
        }

        store_state(slot, TaskState::Waiting);
        promote_waiting_if_pending(slot);
    }

    const size_t remaining = count_ready_tasks();
    if (out_result) {
        out_result->executed = executed;
        out_result->ready_remaining = remaining;
    }
    atomic_store_u8(&g_run_active, 0);

    if (remaining != 0) {
        saturating_add(&g_metrics.budget_exhaustions, 1);
        return Status::BudgetExhausted;
    }
    return Status::Ok;
}

Status yield() {
    if (atomic_load_u8(&g_initialized) == 0) {
        return Status::NotInitialized;
    }
    if (g_current_slot == INVALID_SLOT) {
        return Status::NotRunning;
    }

    TaskSlot& slot = g_tasks[g_current_slot];
    if (load_state(slot) != TaskState::Running) {
        return Status::NotRunning;
    }
    if (atomic_load_u8(&slot.cancel_requested) != 0) {
        return Status::CancellationPending;
    }

    if (atomic_exchange_u8(&slot.yield_requested, 1) == 0) {
        saturating_add(&slot.yield_count, 1);
        saturating_add(&g_metrics.yield_requests, 1);
    }
    return Status::Ok;
}

Status stat(TaskId id, TaskStat* out_stat) {
    if (atomic_load_u8(&g_initialized) == 0) {
        return Status::NotInitialized;
    }
    if (!out_stat) {
        return Status::InvalidArgument;
    }

    size_t index = 0;
    Status status = find_task(id, &index);
    if (status != Status::Ok) {
        return status;
    }

    clear_bytes(out_stat, sizeof(*out_stat));
    copy_task_stat(index, out_stat);
    if (out_stat->id != id || out_stat->state == TaskState::Empty) {
        clear_bytes(out_stat, sizeof(*out_stat));
        return Status::NotFound;
    }
    return Status::Ok;
}

Status list(ListCallback callback, void* context) {
    if (atomic_load_u8(&g_initialized) == 0) {
        return Status::NotInitialized;
    }
    if (!callback) {
        return Status::InvalidArgument;
    }

    TaskId snapshot[MAX_TASKS] = {};
    size_t snapshot_count = 0;
    for (size_t i = 0; i < MAX_TASKS; ++i) {
        if (load_state(g_tasks[i]) != TaskState::Empty) {
            snapshot[snapshot_count++] = atomic_load_u64(&g_tasks[i].id);
        }
    }

    for (size_t i = 0; i < snapshot_count; ++i) {
        TaskStat task = {};
        const Status stat_status = stat(snapshot[i], &task);
        if (stat_status == Status::NotFound) {
            continue;
        }
        if (stat_status != Status::Ok) {
            return stat_status;
        }
        if (!callback(&task, context)) {
            return Status::IterationStopped;
        }
    }
    return Status::Ok;
}

Status get_metrics(SchedulerMetrics* out_metrics) {
    if (atomic_load_u8(&g_initialized) == 0) {
        return Status::NotInitialized;
    }
    if (!out_metrics) {
        return Status::InvalidArgument;
    }

    out_metrics->tick_calls = atomic_load_u64(&g_metrics.tick_calls);
    out_metrics->rejected_tick_calls =
        atomic_load_u64(&g_metrics.rejected_tick_calls);
    out_metrics->tasks_created = atomic_load_u64(&g_metrics.tasks_created);
    out_metrics->tasks_completed = atomic_load_u64(&g_metrics.tasks_completed);
    out_metrics->tasks_cancelled = atomic_load_u64(&g_metrics.tasks_cancelled);
    out_metrics->releases = atomic_load_u64(&g_metrics.releases);
    out_metrics->coalesced_releases =
        atomic_load_u64(&g_metrics.coalesced_releases);
    out_metrics->callbacks_executed =
        atomic_load_u64(&g_metrics.callbacks_executed);
    out_metrics->yield_requests = atomic_load_u64(&g_metrics.yield_requests);
    out_metrics->suspensions = atomic_load_u64(&g_metrics.suspensions);
    out_metrics->resumptions = atomic_load_u64(&g_metrics.resumptions);
    out_metrics->budget_exhaustions =
        atomic_load_u64(&g_metrics.budget_exhaustions);
    return Status::Ok;
}

TaskId current_task() {
    if (g_current_slot == INVALID_SLOT) {
        return INVALID_TASK_ID;
    }
    return atomic_load_u64(&g_tasks[g_current_slot].id);
}

Tick now() {
    return atomic_load_u64(&g_now);
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::NotInitialized: return "not initialized";
        case Status::AlreadyInitialized: return "already initialized";
        case Status::InvalidArgument: return "invalid argument";
        case Status::NameTooLong: return "name too long";
        case Status::PeriodTooLarge: return "period too large";
        case Status::CapacityReached: return "capacity reached";
        case Status::NotFound: return "not found";
        case Status::InvalidState: return "invalid state";
        case Status::AlreadySuspended: return "already suspended";
        case Status::NotSuspended: return "not suspended";
        case Status::NotRunning: return "no task is running";
        case Status::CancellationPending: return "cancellation pending";
        case Status::ClockRegression: return "clock moved backwards";
        case Status::Busy: return "busy";
        case Status::ReentrantCall: return "reentrant call";
        case Status::BudgetExhausted: return "budget exhausted";
        case Status::IterationStopped: return "iteration stopped";
    }
    return "unknown status";
}

} // namespace scheduler

#include "../kernel/task/scheduler.hpp"

namespace {

struct CounterContext {
    size_t calls;
    scheduler::TaskId observed_id;
};

void count_task(void* context) {
    CounterContext* counter = static_cast<CounterContext*>(context);
    ++counter->calls;
    counter->observed_id = scheduler::current_task();
}

struct YieldContext {
    size_t calls;
    scheduler::TaskId id;
    scheduler::Status yield_status;
    scheduler::Status nested_run_status;
};

void yielding_task(void* context) {
    YieldContext* state = static_cast<YieldContext*>(context);
    ++state->calls;
    if (state->calls == 1) {
        scheduler::RunResult nested = {};
        state->nested_run_status = scheduler::run_pending(1, &nested);
        state->yield_status = scheduler::yield();
        state->id = scheduler::current_task();
    }
}

struct SuspendContext {
    size_t calls;
    scheduler::TaskId id;
    scheduler::Status suspend_status;
};

void suspending_task(void* context) {
    SuspendContext* state = static_cast<SuspendContext*>(context);
    ++state->calls;
    if (state->calls == 1) {
        state->suspend_status = scheduler::suspend(state->id);
    }
}

struct CancelSelfContext {
    scheduler::TaskId id;
    size_t calls;
    scheduler::Status cancel_status;
    scheduler::Status yield_after_cancel_status;
};

void cancelling_self_task(void* context) {
    CancelSelfContext* state = static_cast<CancelSelfContext*>(context);
    ++state->calls;
    state->cancel_status = scheduler::cancel(state->id);
    state->yield_after_cancel_status = scheduler::yield();
}

struct KillerContext {
    scheduler::TaskId victim;
    size_t calls;
    scheduler::Status cancel_status;
};

void killer_task(void* context) {
    KillerContext* state = static_cast<KillerContext*>(context);
    ++state->calls;
    state->cancel_status = scheduler::cancel(state->victim);
}

struct SpawnContext {
    CounterContext* child_counter;
    scheduler::TaskId child_id;
    scheduler::Status create_status;
    size_t calls;
};

void spawning_task(void* context) {
    SpawnContext* state = static_cast<SpawnContext*>(context);
    ++state->calls;
    state->create_status = scheduler::create(
        "spawned",
        count_task,
        state->child_counter,
        0,
        &state->child_id
    );
}

struct ListState {
    size_t calls;
    scheduler::Status mutation_status;
};

bool stop_listing(const scheduler::TaskStat*, void* context) {
    ListState* state = static_cast<ListState*>(context);
    ++state->calls;
    return false;
}

bool cancel_during_listing(const scheduler::TaskStat* task, void* context) {
    ListState* state = static_cast<ListState*>(context);
    ++state->calls;
    const scheduler::Status status = scheduler::cancel(task->id);
    if (status != scheduler::Status::Ok) {
        state->mutation_status = status;
        return false;
    }
    return true;
}

bool count_listing(const scheduler::TaskStat*, void* context) {
    size_t* count = static_cast<size_t*>(context);
    ++(*count);
    return true;
}

bool text_equals(const char* left, const char* right) {
    if (!left || !right) {
        return left == right;
    }
    while (*left != '\0' && *right != '\0') {
        if (*left != *right) {
            return false;
        }
        ++left;
        ++right;
    }
    return *left == *right;
}

} // namespace

int main() {
    scheduler::TaskId temporary_id = scheduler::INVALID_TASK_ID;
    if (scheduler::tick(0) != scheduler::Status::NotInitialized ||
        scheduler::create("early", count_task, nullptr, 0, &temporary_id) !=
            scheduler::Status::NotInitialized) {
        return 1;
    }

    const scheduler::Tick wrap_start = UINT64_MAX - 3;
    if (scheduler::initialize(wrap_start) != scheduler::Status::Ok ||
        scheduler::initialize(0) != scheduler::Status::AlreadyInitialized) {
        return 2;
    }

    CounterContext periodic = {};
    scheduler::TaskId periodic_id = scheduler::INVALID_TASK_ID;
    if (scheduler::create("periodic", count_task, &periodic, 2, &periodic_id) !=
        scheduler::Status::Ok) {
        return 3;
    }

    scheduler::TaskStat task_info = {};
    if (scheduler::stat(periodic_id, &task_info) != scheduler::Status::Ok ||
        task_info.state != scheduler::TaskState::Waiting ||
        task_info.next_release != UINT64_MAX - 1) {
        return 4;
    }

    if (scheduler::tick(UINT64_MAX - 2) != scheduler::Status::Ok ||
        periodic.calls != 0 ||
        scheduler::stat(periodic_id, &task_info) != scheduler::Status::Ok ||
        task_info.state != scheduler::TaskState::Waiting) {
        return 5;
    }
    if (scheduler::tick(UINT64_MAX - 1) != scheduler::Status::Ok ||
        periodic.calls != 0 ||
        scheduler::stat(periodic_id, &task_info) != scheduler::Status::Ok ||
        task_info.state != scheduler::TaskState::Ready) {
        return 6;
    }

    scheduler::RunResult run = {};
    if (scheduler::run_pending(0, &run) != scheduler::Status::BudgetExhausted ||
        run.executed != 0 ||
        run.ready_remaining != 1 ||
        periodic.calls != 0) {
        return 7;
    }
    if (scheduler::run_pending(1, &run) != scheduler::Status::Ok ||
        run.executed != 1 ||
        periodic.calls != 1 ||
        periodic.observed_id != periodic_id) {
        return 8;
    }

    if (scheduler::tick(UINT64_MAX) != scheduler::Status::Ok ||
        scheduler::tick(0) != scheduler::Status::Ok ||
        periodic.calls != 1 ||
        scheduler::run_pending(1, &run) != scheduler::Status::Ok ||
        periodic.calls != 2) {
        return 9;
    }
    if (scheduler::tick(UINT64_MAX) != scheduler::Status::ClockRegression ||
        scheduler::now() != 0) {
        return 10;
    }

    scheduler::SchedulerMetrics metrics = {};
    if (scheduler::get_metrics(&metrics) != scheduler::Status::Ok ||
        metrics.tick_calls != 4 ||
        metrics.rejected_tick_calls != 1 ||
        metrics.callbacks_executed != 2 ||
        metrics.budget_exhaustions != 1) {
        return 11;
    }

    if (scheduler::reset(0) != scheduler::Status::Ok) {
        return 12;
    }
    periodic = {};
    if (scheduler::create("coalesce", count_task, &periodic, 2, &periodic_id) !=
            scheduler::Status::Ok ||
        scheduler::tick(10) != scheduler::Status::Ok ||
        periodic.calls != 0 ||
        scheduler::stat(periodic_id, &task_info) != scheduler::Status::Ok ||
        task_info.release_count != 5 ||
        task_info.coalesced_release_count != 4 ||
        !task_info.pending ||
        task_info.next_release != 12) {
        return 13;
    }
    if (scheduler::run_pending(1, &run) != scheduler::Status::Ok ||
        periodic.calls != 1 ||
        scheduler::stat(periodic_id, &task_info) != scheduler::Status::Ok ||
        task_info.state != scheduler::TaskState::Waiting) {
        return 14;
    }

    if (scheduler::reset(100) != scheduler::Status::Ok ||
        scheduler::tick(90) != scheduler::Status::ClockRegression ||
        scheduler::now() != 100 ||
        scheduler::get_metrics(&metrics) != scheduler::Status::Ok ||
        metrics.rejected_tick_calls != 1 ||
        metrics.tick_calls != 0) {
        return 15;
    }

    if (scheduler::reset(0) != scheduler::Status::Ok) {
        return 16;
    }
    YieldContext yielding = {};
    scheduler::TaskId yielding_id = scheduler::INVALID_TASK_ID;
    if (scheduler::create("yielding", yielding_task, &yielding, 0, &yielding_id) !=
        scheduler::Status::Ok) {
        return 17;
    }
    if (scheduler::run_pending(1, &run) != scheduler::Status::BudgetExhausted ||
        run.executed != 1 ||
        run.ready_remaining != 1 ||
        yielding.calls != 1 ||
        yielding.id != yielding_id ||
        yielding.yield_status != scheduler::Status::Ok ||
        yielding.nested_run_status != scheduler::Status::ReentrantCall) {
        return 18;
    }
    if (scheduler::stat(yielding_id, &task_info) != scheduler::Status::Ok ||
        task_info.state != scheduler::TaskState::Ready ||
        task_info.run_count != 1 ||
        task_info.yield_count != 1) {
        return 19;
    }
    if (scheduler::run_pending(1, &run) != scheduler::Status::Ok ||
        yielding.calls != 2 ||
        scheduler::stat(yielding_id, &task_info) != scheduler::Status::NotFound ||
        scheduler::yield() != scheduler::Status::NotRunning) {
        return 20;
    }

    if (scheduler::reset(0) != scheduler::Status::Ok) {
        return 21;
    }
    SuspendContext self_suspend = {};
    scheduler::TaskId suspended_id = scheduler::INVALID_TASK_ID;
    if (scheduler::create(
            "self-suspend",
            suspending_task,
            &self_suspend,
            0,
            &suspended_id
        ) != scheduler::Status::Ok) {
        return 22;
    }
    self_suspend.id = suspended_id;
    if (scheduler::run_pending(1, &run) != scheduler::Status::Ok ||
        self_suspend.calls != 1 ||
        self_suspend.suspend_status != scheduler::Status::Ok ||
        scheduler::stat(suspended_id, &task_info) != scheduler::Status::Ok ||
        task_info.state != scheduler::TaskState::Suspended) {
        return 23;
    }
    if (scheduler::suspend(suspended_id) != scheduler::Status::AlreadySuspended ||
        scheduler::resume(suspended_id) != scheduler::Status::Ok ||
        scheduler::run_pending(1, &run) != scheduler::Status::Ok ||
        self_suspend.calls != 2 ||
        scheduler::stat(suspended_id, &task_info) != scheduler::Status::NotFound) {
        return 24;
    }

    if (scheduler::reset(0) != scheduler::Status::Ok) {
        return 25;
    }
    CancelSelfContext self_cancel = {};
    scheduler::TaskId cancel_id = scheduler::INVALID_TASK_ID;
    if (scheduler::create(
            "self-cancel",
            cancelling_self_task,
            &self_cancel,
            0,
            &cancel_id
        ) != scheduler::Status::Ok) {
        return 26;
    }
    self_cancel.id = cancel_id;
    if (scheduler::run_pending(1, &run) != scheduler::Status::Ok ||
        self_cancel.calls != 1 ||
        self_cancel.cancel_status != scheduler::Status::Ok ||
        self_cancel.yield_after_cancel_status != scheduler::Status::CancellationPending ||
        scheduler::stat(cancel_id, &task_info) != scheduler::Status::NotFound) {
        return 27;
    }

    if (scheduler::reset(0) != scheduler::Status::Ok) {
        return 28;
    }
    KillerContext killer = {};
    CounterContext victim = {};
    scheduler::TaskId killer_id = scheduler::INVALID_TASK_ID;
    scheduler::TaskId victim_id = scheduler::INVALID_TASK_ID;
    if (scheduler::create("killer", killer_task, &killer, 0, &killer_id) !=
            scheduler::Status::Ok ||
        scheduler::create("victim", count_task, &victim, 0, &victim_id) !=
            scheduler::Status::Ok) {
        return 29;
    }
    killer.victim = victim_id;
    if (scheduler::run_pending(2, &run) != scheduler::Status::Ok ||
        run.executed != 1 ||
        killer.calls != 1 ||
        killer.cancel_status != scheduler::Status::Ok ||
        victim.calls != 0 ||
        scheduler::stat(victim_id, &task_info) != scheduler::Status::NotFound) {
        return 30;
    }

    if (scheduler::reset(0) != scheduler::Status::Ok) {
        return 31;
    }
    CounterContext spawned_counter = {};
    SpawnContext spawn = {&spawned_counter, scheduler::INVALID_TASK_ID,
                          scheduler::Status::InvalidState, 0};
    scheduler::TaskId spawner_id = scheduler::INVALID_TASK_ID;
    if (scheduler::create("spawner", spawning_task, &spawn, 0, &spawner_id) !=
            scheduler::Status::Ok ||
        scheduler::run_pending(2, &run) != scheduler::Status::Ok ||
        run.executed != 2 ||
        spawn.calls != 1 ||
        spawn.create_status != scheduler::Status::Ok ||
        spawned_counter.calls != 1 ||
        spawned_counter.observed_id != spawn.child_id) {
        return 32;
    }

    if (scheduler::reset(0) != scheduler::Status::Ok) {
        return 33;
    }
    CounterContext fair[3] = {};
    scheduler::TaskId fair_ids[3] = {};
    for (size_t i = 0; i < 3; ++i) {
        if (scheduler::create("fair", count_task, &fair[i], 0, &fair_ids[i]) !=
            scheduler::Status::Ok) {
            return 34;
        }
    }
    if (scheduler::run_pending(2, &run) != scheduler::Status::BudgetExhausted ||
        run.executed != 2 ||
        run.ready_remaining != 1 ||
        fair[0].calls != 1 ||
        fair[1].calls != 1 ||
        fair[2].calls != 0) {
        return 35;
    }
    if (scheduler::run_pending(1, &run) != scheduler::Status::Ok ||
        fair[2].calls != 1) {
        return 36;
    }

    if (scheduler::reset(0) != scheduler::Status::Ok) {
        return 37;
    }
    CounterContext shared_counter = {};
    scheduler::TaskId ids[scheduler::MAX_TASKS] = {};
    for (size_t i = 0; i < scheduler::MAX_TASKS; ++i) {
        if (scheduler::create("capacity", count_task, &shared_counter, 0, &ids[i]) !=
            scheduler::Status::Ok) {
            return 38;
        }
    }
    if (scheduler::create(
            "overflow",
            count_task,
            &shared_counter,
            0,
            &temporary_id
        ) != scheduler::Status::CapacityReached) {
        return 39;
    }

    ListState listing = {};
    if (scheduler::list(stop_listing, &listing) != scheduler::Status::IterationStopped ||
        listing.calls != 1) {
        return 40;
    }

    const scheduler::TaskId stale_id = ids[0];
    if (scheduler::cancel(stale_id) != scheduler::Status::Ok ||
        scheduler::create(
            "replacement",
            count_task,
            &shared_counter,
            0,
            &temporary_id
        ) != scheduler::Status::Ok ||
        temporary_id == stale_id ||
        scheduler::cancel(stale_id) != scheduler::Status::NotFound) {
        return 41;
    }

    listing = {};
    listing.mutation_status = scheduler::Status::Ok;
    if (scheduler::list(cancel_during_listing, &listing) != scheduler::Status::Ok ||
        listing.calls != scheduler::MAX_TASKS ||
        listing.mutation_status != scheduler::Status::Ok) {
        return 42;
    }
    size_t listed_after_cancel = 0;
    if (scheduler::list(count_listing, &listed_after_cancel) != scheduler::Status::Ok ||
        listed_after_cancel != 0) {
        return 43;
    }

    if (scheduler::reset(0) != scheduler::Status::Ok) {
        return 44;
    }
    char long_name[scheduler::MAX_TASK_NAME_LENGTH + 2] = {};
    for (size_t i = 0; i <= scheduler::MAX_TASK_NAME_LENGTH; ++i) {
        long_name[i] = 'x';
    }
    long_name[scheduler::MAX_TASK_NAME_LENGTH + 1] = '\0';
    if (scheduler::create("", count_task, nullptr, 0, &temporary_id) !=
            scheduler::Status::InvalidArgument ||
        scheduler::create(long_name, count_task, nullptr, 0, &temporary_id) !=
            scheduler::Status::NameTooLong ||
        scheduler::create("null", nullptr, nullptr, 0, &temporary_id) !=
            scheduler::Status::InvalidArgument ||
        scheduler::create(
            "long-period",
            count_task,
            nullptr,
            scheduler::MAX_FORWARD_TICK_DELTA + 1,
            &temporary_id
        ) != scheduler::Status::PeriodTooLarge ||
        scheduler::create("no-output", count_task, nullptr, 0, nullptr) !=
            scheduler::Status::InvalidArgument) {
        return 45;
    }

    CounterContext external_suspend = {};
    scheduler::TaskId external_id = scheduler::INVALID_TASK_ID;
    if (scheduler::create(
            "external-suspend",
            count_task,
            &external_suspend,
            0,
            &external_id
        ) != scheduler::Status::Ok ||
        scheduler::resume(external_id) != scheduler::Status::NotSuspended ||
        scheduler::suspend(external_id) != scheduler::Status::Ok ||
        scheduler::suspend(external_id) != scheduler::Status::AlreadySuspended ||
        scheduler::run_pending(1, &run) != scheduler::Status::Ok ||
        external_suspend.calls != 0 ||
        scheduler::resume(external_id) != scheduler::Status::Ok ||
        scheduler::run_pending(1, &run) != scheduler::Status::Ok ||
        external_suspend.calls != 1) {
        return 46;
    }

    if (scheduler::stat(scheduler::INVALID_TASK_ID, &task_info) !=
            scheduler::Status::NotFound ||
        scheduler::stat(external_id, nullptr) != scheduler::Status::InvalidArgument ||
        scheduler::list(nullptr, nullptr) != scheduler::Status::InvalidArgument ||
        scheduler::get_metrics(nullptr) != scheduler::Status::InvalidArgument ||
        scheduler::cancel(scheduler::INVALID_TASK_ID) != scheduler::Status::NotFound ||
        scheduler::suspend(scheduler::INVALID_TASK_ID) != scheduler::Status::NotFound ||
        scheduler::resume(scheduler::INVALID_TASK_ID) != scheduler::Status::NotFound) {
        return 47;
    }

    if (!text_equals(
            scheduler::status_message(scheduler::Status::BudgetExhausted),
            "budget exhausted"
        )) {
        return 48;
    }

    return 0;
}

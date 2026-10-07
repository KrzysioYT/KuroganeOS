#include "../kernel/task/scheduler2.hpp"

#include <cassert>
#include <cstdint>

int main() {
    using namespace threading::scheduler2;

    assert(initialize(0U) == Status::InvalidArgument);
    assert(initialize(4U) == Status::Ok);
    assert(initialize(4U) == Status::AlreadyInitialized);
    assert(cpu_count() == 4U);
    assert(online_mask() == UINT64_C(0xF));

    assert(register_thread(101U, 5U, UINT64_C(0x3), 0U) == Status::Ok);
    assert(register_thread(102U, 9U, UINT64_C(0x1), 0U) == Status::Ok);
    assert(register_thread(103U, 7U, UINT64_C(0x4), 2U) == Status::Ok);
    assert(register_thread(103U, 7U, UINT64_C(0x4), 2U) ==
           Status::AlreadyExists);
    assert(register_thread(104U, 40U, UINT64_C(0x1), 0U) ==
           Status::InvalidArgument);
    assert(register_thread(104U, 1U, UINT64_C(0x10), 0U) ==
           Status::InvalidAffinity);

    ThreadId selected = INVALID_THREAD_ID;
    assert(pick_next(0U, INVALID_THREAD_ID, &selected) == Status::Ok);
    assert(selected == 102U);

    assert(set_state(102U, State::Blocked) == Status::Ok);
    assert(pick_next(0U, INVALID_THREAD_ID, &selected) == Status::Ok);
    assert(selected == 101U);

    // CPU1 starts empty. Thread 101 is affinity-eligible there, so the idle
    // queue steals it from CPU0 and records a real migration.
    assert(pick_next(1U, INVALID_THREAD_ID, &selected) == Status::Ok);
    assert(selected == 101U);
    ThreadStat migrated{};
    assert(stat(101U, &migrated) == Status::Ok);
    assert(migrated.home_cpu == 1U);
    assert(migrated.last_cpu == 1U);
    assert(migrated.migrations == 1U);

    assert(pick_next(3U, INVALID_THREAD_ID, &selected) == Status::NotFound);
    assert(set_affinity(103U, UINT64_C(0x8)) == Status::Ok);
    ThreadStat affinity_changed{};
    assert(stat(103U, &affinity_changed) == Status::Ok);
    assert(affinity_changed.home_cpu == 3U);
    assert(affinity_changed.migrations == 1U);
    assert(pick_next(3U, INVALID_THREAD_ID, &selected) == Status::Ok);
    assert(selected == 103U);

    CpuStat cpu1{};
    assert(cpu_stat(1U, &cpu1) == Status::Ok);
    assert(cpu1.steals == 1U);
    assert(cpu1.dispatches >= 1U);

    // Equal-priority local threads must round-robin instead of pinning the
    // first slot forever.
    assert(reset(1U) == Status::Ok);
    assert(register_thread(201U, 3U, UINT64_C(1), 0U) == Status::Ok);
    assert(expand_cpu_count(4U) == Status::Ok);
    assert(cpu_count() == 4U);
    assert(online_mask() == UINT64_C(0xF));
    assert(expand_cpu_count(2U) == Status::InvalidArgument);
    assert(set_affinity(201U, UINT64_C(0xF)) == Status::Ok);
    assert(register_thread(202U, 3U, UINT64_C(1), 0U) == Status::Ok);
    assert(register_thread(203U, 3U, UINT64_C(1), 0U) == Status::Ok);
    ThreadId first = 0U;
    ThreadId second = 0U;
    ThreadId third = 0U;
    assert(pick_next(0U, INVALID_THREAD_ID, &first) == Status::Ok);
    assert(pick_next(0U, INVALID_THREAD_ID, &second) == Status::Ok);
    assert(pick_next(0U, INVALID_THREAD_ID, &third) == Status::Ok);
    assert(first == 201U && second == 202U && third == 203U);

    assert(set_priority(203U, 8U) == Status::Ok);
    assert(pick_next(0U, INVALID_THREAD_ID, &selected) == Status::Ok);
    assert(selected == 203U);
    assert(set_state(203U, State::Sleeping) == Status::Ok);
    assert(pick_next(0U, 201U, &selected) == Status::Ok);
    assert(selected == 202U);

    assert(unregister_thread(202U) == Status::Ok);
    assert(unregister_thread(202U) == Status::NotFound);
    assert(stat(202U, &migrated) == Status::NotFound);
    return 0;
}

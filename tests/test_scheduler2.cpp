#include "../kernel/task/scheduler2.hpp"

#include <cassert>
#include <iostream>

int main() {
    using namespace scheduler2;

    RunQueueSet set{};
    assert(initialize(&set, 4U) == Status::Ok);
    assert(online_mask(set) == 0x0FU);

    assert(enqueue(&set, 1U, 0x0FU) == Status::Ok);
    assert(enqueue(&set, 2U, 0x0FU) == Status::Ok);
    assert(enqueue(&set, 3U, 0x02U) == Status::Ok);
    assert(enqueue(&set, 4U, 0x0CU, 3U) == Status::Ok);
    assert(enqueue(&set, 4U, 0x0FU) == Status::DuplicateRunnable);
    assert(enqueue(&set, 5U, 0U) == Status::EmptyAffinity);

    RunnableStat pinned{};
    assert(runnable_stat(set, 3U, &pinned));
    assert(pinned.assigned_cpu == 1U);
    assert(cpu_allowed(set, pinned.affinity, pinned.assigned_cpu));

    QueueStat q0{}, q1{}, q2{}, q3{};
    assert(queue_stat(set, 0U, &q0));
    assert(queue_stat(set, 1U, &q1));
    assert(queue_stat(set, 2U, &q2));
    assert(queue_stat(set, 3U, &q3));
    assert(q0.runnable_count + q1.runnable_count +
           q2.runnable_count + q3.runnable_count == 4U);

    assert(migrate(&set, 1U, 0x08U, 3U) == Status::Ok);
    RunnableStat migrated{};
    assert(runnable_stat(set, 1U, &migrated));
    assert(migrated.assigned_cpu == 3U);
    assert(migrated.affinity == 0x08U);

    RunnableId id = INVALID_RUNNABLE_ID;
    assert(dequeue(&set, 1U, &id) == Status::Ok);
    assert(id == 3U);
    assert(!runnable_stat(set, 3U, &pinned));

    // CPU3 currently owns runnable 1, which is pinned exclusively there.
    // Shrinking to two CPUs must be rejected without corrupting queues.
    assert(set_online_cpu_count(&set, 2U) == Status::EmptyAffinity);
    assert(set.online_cpu_count == 4U);
    assert(runnable_stat(set, 1U, &migrated));
    assert(migrated.assigned_cpu == 3U);

    assert(migrate(&set, 1U, 0x03U, 0U) == Status::Ok);
    assert(set_online_cpu_count(&set, 2U) == Status::Ok);
    assert(set.online_cpu_count == 2U);
    assert((online_mask(set) & ~0x03U) == 0U);

    assert(remove(&set, 2U) == Status::Ok);
    assert(remove(&set, 2U) == Status::NotFound);

    std::cout << "Scheduler 2.0 per-CPU run queue policy: PASS\n";
    return 0;
}

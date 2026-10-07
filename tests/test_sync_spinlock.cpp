#include "../kernel/sync/spinlock.hpp"

#include <cassert>
#include <cstdint>
#include <thread>
#include <vector>

int main() {
    sync::TicketSpinLock lock;

    assert(lock.try_lock());
    assert(!lock.try_lock());
    lock.unlock();

    constexpr size_t worker_count = 8U;
    constexpr size_t iterations = 20000U;
    uint64_t protected_counter = 0U;
    std::vector<std::thread> workers;
    workers.reserve(worker_count);

    for (size_t worker = 0U; worker < worker_count; ++worker) {
        workers.emplace_back([&]() {
            for (size_t iteration = 0U; iteration < iterations; ++iteration) {
                sync::LockGuard guard(lock);
                ++protected_counter;
            }
        });
    }
    for (std::thread& worker : workers) worker.join();

    assert(protected_counter == worker_count * iterations);

    // The lock must be reusable after heavy contention.
    assert(lock.try_lock());
    lock.unlock();
    return 0;
}

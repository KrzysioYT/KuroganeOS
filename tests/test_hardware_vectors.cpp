#include <assert.h>
#include <stdint.h>
#include <thread>

#include "../kernel/arch/x86_64/hardware_vectors.hpp"

namespace vectors = arch::x86_64::hardware_vectors;

int main() {
    vectors::Lease lease{};
    assert(vectors::allocate(nullptr) == vectors::Status::InvalidArgument);
    assert(vectors::allocate(&lease) == vectors::Status::NotInitialized);
    assert(!vectors::initialized());
    assert(vectors::allocated_count() == 0U);

    vectors::initialize();
    assert(vectors::initialized());
    assert(vectors::capacity() == 175U);
    assert(!vectors::is_allocatable(0x3FU));
    assert(vectors::is_allocatable(0x40U));
    assert(!vectors::is_allocatable(0x80U));
    assert(vectors::is_allocatable(0x81U));
    assert(!vectors::is_allocatable(0xF0U));

    vectors::Lease leases[vectors::VECTOR_CAPACITY]{};
    bool seen[256]{};
    for (size_t index = 0U; index < vectors::VECTOR_CAPACITY; ++index) {
        assert(vectors::allocate(&leases[index]) == vectors::Status::Ok);
        assert(leases[index].generation != 0U);
        assert(leases[index].vector != vectors::RESERVED_SYSCALL_VECTOR);
        assert(!seen[leases[index].vector]);
        seen[leases[index].vector] = true;
        assert(vectors::owns(leases[index]));
        assert(vectors::claimed(leases[index].vector));
    }
    assert(vectors::allocated_count() == vectors::capacity());
    assert(vectors::allocate(&lease) == vectors::Status::Exhausted);

    const vectors::Lease old = leases[17U];
    assert(vectors::release(old) == vectors::Status::Ok);
    assert(!vectors::owns(old));
    assert(!vectors::claimed(old.vector));
    assert(vectors::release(old) == vectors::Status::StaleLease);

    vectors::Lease reused{};
    assert(vectors::allocate(&reused) == vectors::Status::Ok);
    assert(reused.vector == old.vector);
    assert(reused.generation != old.generation);
    assert(vectors::owns(reused));
    assert(vectors::release(old) == vectors::Status::StaleLease);
    assert(vectors::owns(reused));

    assert(vectors::release({0x80U, 1U}) == vectors::Status::InvalidArgument);
    assert(vectors::release({0x40U, 0U}) == vectors::Status::InvalidArgument);

    for (size_t index = 0U; index < vectors::VECTOR_CAPACITY; ++index) {
        if (index == 17U) continue;
        assert(vectors::release(leases[index]) == vectors::Status::Ok);
    }
    assert(vectors::release(reused) == vectors::Status::Ok);
    assert(vectors::allocated_count() == 0U);

    // Exercise the same CAS path from several future CPU contexts. No vector
    // may be issued twice, and parallel release must return the pool to zero.
    constexpr size_t worker_count = 7U;
    constexpr size_t leases_per_worker = 20U;
    vectors::initialize();
    vectors::Lease concurrent[worker_count][leases_per_worker]{};
    std::thread workers[worker_count];
    for (size_t worker = 0U; worker < worker_count; ++worker) {
        workers[worker] = std::thread([worker, &concurrent]() {
            for (size_t index = 0U; index < leases_per_worker; ++index) {
                assert(vectors::allocate(&concurrent[worker][index]) ==
                    vectors::Status::Ok);
            }
        });
    }
    for (auto& worker : workers) worker.join();
    assert(vectors::allocated_count() == worker_count * leases_per_worker);
    bool concurrent_seen[256]{};
    for (size_t worker = 0U; worker < worker_count; ++worker) {
        for (size_t index = 0U; index < leases_per_worker; ++index) {
            const auto& current = concurrent[worker][index];
            assert(!concurrent_seen[current.vector]);
            concurrent_seen[current.vector] = true;
        }
    }
    for (size_t worker = 0U; worker < worker_count; ++worker) {
        workers[worker] = std::thread([worker, &concurrent]() {
            for (size_t index = 0U; index < leases_per_worker; ++index) {
                assert(vectors::release(concurrent[worker][index]) ==
                    vectors::Status::Ok);
            }
        });
    }
    for (auto& worker : workers) worker.join();
    assert(vectors::allocated_count() == 0U);
    return 0;
}

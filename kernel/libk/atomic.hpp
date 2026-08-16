#pragma once

#include <stdint.h>

namespace libk {

enum class MemoryOrder : int {
    Relaxed = __ATOMIC_RELAXED,
    Acquire = __ATOMIC_ACQUIRE,
    Release = __ATOMIC_RELEASE,
    AcquireRelease = __ATOMIC_ACQ_REL,
    Sequential = __ATOMIC_SEQ_CST,
};

template<typename T>
class Atomic {
public:
    constexpr explicit Atomic(T value = {}) : value_(value) {}
    Atomic(const Atomic&) = delete;
    Atomic& operator=(const Atomic&) = delete;

    T load(MemoryOrder order = MemoryOrder::Sequential) const {
        return __atomic_load_n(&value_, static_cast<int>(order));
    }

    void store(T value, MemoryOrder order = MemoryOrder::Sequential) {
        __atomic_store_n(&value_, value, static_cast<int>(order));
    }

    T exchange(T value, MemoryOrder order = MemoryOrder::Sequential) {
        return __atomic_exchange_n(&value_, value, static_cast<int>(order));
    }

    bool compare_exchange(
        T& expected,
        T desired,
        MemoryOrder success = MemoryOrder::Sequential,
        MemoryOrder failure = MemoryOrder::Sequential) {
        return __atomic_compare_exchange_n(
            &value_,
            &expected,
            desired,
            false,
            static_cast<int>(success),
            static_cast<int>(failure));
    }

private:
    mutable T value_;
};

} // namespace libk

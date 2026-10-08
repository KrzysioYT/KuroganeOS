#pragma once

#include <stdint.h>

namespace sync {

inline void cpu_relax() {
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ volatile("pause" : : : "memory");
#else
    __asm__ volatile("" : : : "memory");
#endif
}

// FIFO spin lock for very short kernel critical sections.
//
// Unlike cli/sti, this serializes access between different processors. It does
// not sleep and therefore must never protect blocking or long-running work.
// Interrupt-context users must keep their existing local interrupt discipline
// so an interrupt on the same CPU cannot recursively acquire the same lock.
class TicketSpinLock {
public:
    TicketSpinLock() = default;
    TicketSpinLock(const TicketSpinLock&) = delete;
    TicketSpinLock& operator=(const TicketSpinLock&) = delete;

    void lock() {
        const uint32_t ticket =
            __atomic_fetch_add(&next_ticket_, uint32_t{1U}, __ATOMIC_RELAXED);
        while (__atomic_load_n(&serving_, __ATOMIC_ACQUIRE) != ticket) {
            cpu_relax();
        }
    }

    bool try_lock() {
        uint32_t serving = __atomic_load_n(&serving_, __ATOMIC_ACQUIRE);
        uint32_t next = __atomic_load_n(&next_ticket_, __ATOMIC_RELAXED);
        if (next != serving) return false;
        return __atomic_compare_exchange_n(
            &next_ticket_,
            &next,
            next + uint32_t{1U},
            false,
            __ATOMIC_ACQUIRE,
            __ATOMIC_RELAXED);
    }

    void unlock() {
        __atomic_fetch_add(&serving_, uint32_t{1U}, __ATOMIC_RELEASE);
    }

private:
    alignas(64) uint32_t next_ticket_{0U};
    alignas(64) uint32_t serving_{0U};
};

class LockGuard {
public:
    explicit LockGuard(TicketSpinLock& lock) : lock_(lock) {
        lock_.lock();
    }

    ~LockGuard() {
        lock_.unlock();
    }

    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

private:
    TicketSpinLock& lock_;
};

} // namespace sync

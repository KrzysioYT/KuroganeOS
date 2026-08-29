#include "event_ring.hpp"

namespace diagnostics::events {
namespace {

struct Slot {
    uint64_t guard;
    Event event;
};

alignas(64) Slot g_slots[EVENT_CAPACITY]{};
alignas(8) uint64_t g_next_sequence = 0U;

void copy_bounded(char* destination, size_t capacity, const char* source) {
    if (destination == nullptr || capacity == 0U) {
        return;
    }
    if (source == nullptr) {
        source = "(null)";
    }
    size_t index = 0U;
    while (index + 1U < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
    for (++index; index < capacity; ++index) {
        destination[index] = '\0';
    }
}

uint64_t writing_guard(uint64_t sequence) {
    return (sequence << 1U) | UINT64_C(1);
}

uint64_t complete_guard(uint64_t sequence) {
    return (sequence << 1U) | UINT64_C(2);
}

} // namespace

void record(
    uint64_t monotonic_tick,
    const char* subsystem,
    Severity severity,
    uint32_t cpu,
    uint64_t pid,
    uint64_t tid,
    uint16_t code,
    const char* message) {
    const uint64_t sequence = __atomic_fetch_add(
        &g_next_sequence, UINT64_C(1), __ATOMIC_RELAXED);
    Slot& slot = g_slots[sequence % EVENT_CAPACITY];

    __atomic_store_n(&slot.guard, writing_guard(sequence), __ATOMIC_RELEASE);
    slot.event.sequence = sequence;
    slot.event.monotonic_tick = monotonic_tick;
    slot.event.pid = pid;
    slot.event.tid = tid;
    slot.event.cpu = cpu;
    slot.event.code = code;
    slot.event.severity = severity;
    copy_bounded(slot.event.subsystem, SUBSYSTEM_CAPACITY, subsystem);
    copy_bounded(slot.event.message, MESSAGE_CAPACITY, message);
    __atomic_store_n(&slot.guard, complete_guard(sequence), __ATOMIC_RELEASE);
}

size_t copy_recent(Event* output, size_t capacity) {
    if (output == nullptr || capacity == 0U) {
        return 0U;
    }

    const uint64_t end = __atomic_load_n(&g_next_sequence, __ATOMIC_ACQUIRE);
    uint64_t available = end < EVENT_CAPACITY ? end : EVENT_CAPACITY;
    if (available > capacity) {
        available = capacity;
    }
    const uint64_t start = end - available;
    size_t copied = 0U;

    for (uint64_t sequence = start; sequence < end; ++sequence) {
        Slot& slot = g_slots[sequence % EVENT_CAPACITY];
        const uint64_t expected = complete_guard(sequence);
        const uint64_t before = __atomic_load_n(&slot.guard, __ATOMIC_ACQUIRE);
        if (before != expected) {
            continue;
        }

        Event candidate = slot.event;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        const uint64_t after = __atomic_load_n(&slot.guard, __ATOMIC_ACQUIRE);
        if (after != expected || candidate.sequence != sequence) {
            continue;
        }
        output[copied++] = candidate;
    }
    return copied;
}

uint64_t total_recorded() {
    return __atomic_load_n(&g_next_sequence, __ATOMIC_ACQUIRE);
}

#if defined(KUROGANE_HOST_TEST)
void reset_for_test() {
    __atomic_store_n(&g_next_sequence, UINT64_C(0), __ATOMIC_RELEASE);
    for (Slot& slot : g_slots) {
        __atomic_store_n(&slot.guard, UINT64_C(0), __ATOMIC_RELEASE);
        slot.event = {};
    }
}
#endif

} // namespace diagnostics::events

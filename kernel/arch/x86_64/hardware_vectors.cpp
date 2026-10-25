#include "hardware_vectors.hpp"

#include <limits.h>

namespace arch::x86_64::hardware_vectors {
namespace {

constexpr size_t kVectorSpan =
    static_cast<size_t>(LAST_VECTOR - FIRST_VECTOR + 1U);
constexpr uint64_t kClaimedBit = UINT64_C(1);
constexpr uint64_t kMaximumGeneration = UINT64_MAX >> 1U;

alignas(8) uint64_t g_states[kVectorSpan]{};
alignas(8) size_t g_allocated_count = 0U;
bool g_initialized = false;

size_t index_for(uint8_t vector) {
    return static_cast<size_t>(vector - FIRST_VECTOR);
}

uint64_t encoded_state(uint64_t generation, bool claimed_state) {
    return (generation << 1U) | (claimed_state ? kClaimedBit : UINT64_C(0));
}

uint64_t next_generation(uint64_t current) {
    return current >= kMaximumGeneration ? UINT64_C(1) : current + UINT64_C(1);
}

} // namespace

void initialize() {
    for (size_t index = 0U; index < kVectorSpan; ++index) {
        __atomic_store_n(
            &g_states[index],
            encoded_state(UINT64_C(1), false),
            __ATOMIC_RELAXED);
    }
    __atomic_store_n(&g_allocated_count, static_cast<size_t>(0U), __ATOMIC_RELAXED);
    __atomic_store_n(&g_initialized, true, __ATOMIC_RELEASE);
}

bool initialized() {
    return __atomic_load_n(&g_initialized, __ATOMIC_ACQUIRE);
}

bool is_allocatable(uint8_t vector) {
    return vector >= FIRST_VECTOR && vector <= LAST_VECTOR &&
        vector != RESERVED_SYSCALL_VECTOR;
}

Status allocate(Lease* output) {
    if (output == nullptr) return Status::InvalidArgument;
    *output = {};
    if (!initialized()) return Status::NotInitialized;

    for (uint16_t candidate = FIRST_VECTOR; candidate <= LAST_VECTOR; ++candidate) {
        const uint8_t vector = static_cast<uint8_t>(candidate);
        if (!is_allocatable(vector)) continue;

        uint64_t observed = __atomic_load_n(
            &g_states[index_for(vector)], __ATOMIC_ACQUIRE);
        while ((observed & kClaimedBit) == 0U) {
            const uint64_t claimed_state = observed | kClaimedBit;
            if (__atomic_compare_exchange_n(
                    &g_states[index_for(vector)],
                    &observed,
                    claimed_state,
                    false,
                    __ATOMIC_ACQ_REL,
                    __ATOMIC_ACQUIRE)) {
                __atomic_fetch_add(
                    &g_allocated_count,
                    static_cast<size_t>(1U),
                    __ATOMIC_RELAXED);
                output->vector = vector;
                output->generation = claimed_state >> 1U;
                return Status::Ok;
            }
        }
    }
    return Status::Exhausted;
}

Status release(const Lease& lease) {
    if (!is_allocatable(lease.vector) || lease.generation == 0U) {
        return Status::InvalidArgument;
    }
    if (!initialized()) return Status::NotInitialized;

    uint64_t expected = encoded_state(lease.generation, true);
    const uint64_t replacement = encoded_state(
        next_generation(lease.generation), false);
    if (!__atomic_compare_exchange_n(
            &g_states[index_for(lease.vector)],
            &expected,
            replacement,
            false,
            __ATOMIC_ACQ_REL,
            __ATOMIC_ACQUIRE)) {
        return Status::StaleLease;
    }
    __atomic_fetch_sub(
        &g_allocated_count,
        static_cast<size_t>(1U),
        __ATOMIC_RELAXED);
    return Status::Ok;
}

bool owns(const Lease& lease) {
    if (!initialized() || !is_allocatable(lease.vector) ||
        lease.generation == 0U) {
        return false;
    }
    return __atomic_load_n(
        &g_states[index_for(lease.vector)], __ATOMIC_ACQUIRE) ==
        encoded_state(lease.generation, true);
}

bool claimed(uint8_t vector) {
    if (!initialized() || !is_allocatable(vector)) return false;
    return (__atomic_load_n(
        &g_states[index_for(vector)], __ATOMIC_ACQUIRE) & kClaimedBit) != 0U;
}

size_t capacity() {
    return VECTOR_CAPACITY;
}

size_t allocated_count() {
    return initialized()
        ? __atomic_load_n(&g_allocated_count, __ATOMIC_ACQUIRE)
        : 0U;
}

const char* status_name(Status status) {
    switch (status) {
        case Status::Ok: return "OK";
        case Status::InvalidArgument: return "INVALID_ARGUMENT";
        case Status::NotInitialized: return "NOT_INITIALIZED";
        case Status::Exhausted: return "EXHAUSTED";
        case Status::StaleLease: return "STALE_LEASE";
    }
    return "UNKNOWN";
}

} // namespace arch::x86_64::hardware_vectors

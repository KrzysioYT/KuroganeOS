#pragma once

#include <stddef.h>
#include <stdint.h>

namespace diagnostics::events {

constexpr size_t EVENT_CAPACITY = 64U;
constexpr size_t SUBSYSTEM_CAPACITY = 24U;
constexpr size_t MESSAGE_CAPACITY = 96U;
constexpr uint32_t UNKNOWN_CPU = UINT32_MAX;

enum class Severity : uint8_t {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
};

struct Event {
    uint64_t sequence;
    uint64_t monotonic_tick;
    uint64_t pid;
    uint64_t tid;
    uint32_t cpu;
    uint16_t code;
    Severity severity;
    char subsystem[SUBSYSTEM_CAPACITY];
    char message[MESSAGE_CAPACITY];
};

// Allocation-free writer intended for kernel diagnostic paths. Strings are
// copied into fixed-size records and always NUL terminated. Callers must pass
// only bounded diagnostic text: never arbitrary user payloads or secrets.
void record(
    uint64_t monotonic_tick,
    const char* subsystem,
    Severity severity,
    uint32_t cpu,
    uint64_t pid,
    uint64_t tid,
    uint16_t code,
    const char* message);

// Copies the newest complete records in chronological order. Concurrently
// overwritten/torn records are skipped rather than returned as trusted state.
size_t copy_recent(Event* output, size_t capacity);
uint64_t total_recorded();

#if defined(KUROGANE_HOST_TEST)
void reset_for_test();
#endif

} // namespace diagnostics::events

#pragma once

#include "../arch/x86_64/interrupts.hpp"
#include "event_ring.hpp"

#include <stddef.h>
#include <stdint.h>

namespace diagnostics::panic {

constexpr size_t TRACE_CAPACITY = 16U;
constexpr size_t SNAPSHOT_EVENT_CAPACITY = 16U;
constexpr size_t NAME_CAPACITY = 32U;
constexpr size_t IDENTITY_CAPACITY = 48U;
constexpr uint32_t DUMP_FORMAT_VERSION = 1U;

enum class Context : uint8_t {
    Kernel = 0,
    Userspace,
    Interrupt,
    Unknown,
};

enum class DumpStatus : uint8_t {
    UnavailableNoSafeWriter = 0,
    Written,
    WriteFailed,
};

struct FatalSnapshot {
    uint64_t panic_sequence;
    uint64_t monotonic_tick;
    uint64_t pid;
    uint64_t tid;
    uint64_t cr2;
    uint64_t vmm_root;
    uint64_t pmm_total_frames;
    uint64_t pmm_used_frames;
    uint64_t pmm_free_frames;
    uint32_t cpu;
    uint8_t vector;
    Context context;
    bool cr2_valid;
    bool pmm_available;
    bool vmm_available;
    bool wall_time_trustworthy;
    DumpStatus dump_status;
    arch::x86_64::interrupts::InterruptFrame registers;
    uint64_t trace[TRACE_CAPACITY];
    size_t trace_count;
    events::Event events[SNAPSHOT_EVENT_CAPACITY];
    size_t event_count;
    char reason[IDENTITY_CAPACITY];
    char subsystem[NAME_CAPACITY];
    char process_name[NAME_CAPACITY];
    char thread_name[NAME_CAPACITY];
    char version[NAME_CAPACITY];
    char build_id[IDENTITY_CAPACITY];
    char commit_id[IDENTITY_CAPACITY];
};

struct PanicDump {
    uint8_t magic[8];
    uint32_t format_version;
    uint32_t total_size;
    FatalSnapshot snapshot;
    uint32_t checksum;
};

using PanicSafeDumpWriter = bool (*)(const void* data, size_t size);

// Register only a writer whose implementation has been separately qualified to
// avoid heap allocation, sleeping, filesystem locks, and unsafe controller I/O
// after the fatal transition. No writer is installed by default.
bool register_panic_safe_dump_writer(PanicSafeDumpWriter writer);
void unregister_panic_safe_dump_writer(PanicSafeDumpWriter writer);

const FatalSnapshot& last_snapshot();
const char* exception_name(uint8_t vector);
const char* context_name(Context context);
const char* dump_status_message(DumpStatus status);

[[noreturn]] void fatal_exception(
    arch::x86_64::interrupts::InterruptFrame& frame);

#if defined(KUROGANE_PANIC_TEST) && KUROGANE_PANIC_TEST
[[noreturn]] void inject_invalid_opcode();
void arm_nested_fallback_for_test();
#endif

} // namespace diagnostics::panic

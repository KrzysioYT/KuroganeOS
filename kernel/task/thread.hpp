#pragma once

#include <stddef.h>
#include <stdint.h>

namespace arch::x86_64::interrupts {
struct InterruptFrame;
}

namespace memory::kernel_virtual_memory {
struct OwnedAddressSpace;
}

namespace threading {

using ThreadId = uint64_t;
using Entry = void (*)(void* argument);
using PreDispatchHook = void (*)();

constexpr ThreadId INVALID_THREAD_ID = 0U;
constexpr size_t MAX_THREADS = 16U;
constexpr size_t MAX_THREAD_NAME = 31U;

// Process threads suspend their ordinary kernel call chain in the lower part
// of this stack while Ring-3 executes.  An interrupt/syscall then enters from
// the physical top through the TSS RSP0 and may run deep filesystem/storage
// code before returning to the suspended runtime::run() frame.  FAT32 path
// mutation legitimately needs more than 32 KiB at that boundary, so keep a
// dedicated 64 KiB entry region while preserving the original 32 KiB kernel
// execution region below it.  The split is bounded and explicit: overflowing
// either half remains a bug, but normal VFS/AHCI syscall depth must never
// overwrite the suspended Ring-3 launch return chain.
constexpr size_t KERNEL_EXECUTION_RESERVE = 32U * 1024U;
constexpr size_t KERNEL_ENTRY_RESERVE = 64U * 1024U;
constexpr size_t KERNEL_STACK_SIZE =
    KERNEL_EXECUTION_RESERVE + KERNEL_ENTRY_RESERVE;
static_assert(KERNEL_STACK_SIZE == 96U * 1024U);
static_assert(KERNEL_ENTRY_RESERVE < KERNEL_STACK_SIZE);

enum class Status : uint8_t {
    Ok = 0,
    NotInitialized,
    AlreadyInitialized,
    InvalidArgument,
    NameTooLong,
    CapacityReached,
    NotFound,
    NotRunning,
    Busy,
    BudgetExhausted,
    CorruptContext
};

enum class State : uint8_t {
    Empty = 0,
    New,
    Ready,
    Running,
    Blocked,
    Sleeping,
    Terminated
};

struct Stat {
    ThreadId id;
    char name[MAX_THREAD_NAME + 1U];
    State state;
    uint64_t switches;
    uintptr_t stack_bottom;
    uintptr_t stack_top;
    uintptr_t user_stack;
    uint64_t process_id;
    uint64_t address_space_root;
    uint64_t wake_tick;
    uint8_t priority;
};

struct RunResult {
    uint64_t switches;
    uint64_t completed;
    size_t ready_remaining;
};

struct PreemptRunResult {
    uint64_t preemptions;
    uint64_t completed;
    uint64_t timer_ticks;
    bool timed_out;
};

using ListCallback = bool (*)(const Stat& stat, void* context);

Status initialize();
Status set_pre_dispatch_hook(PreDispatchHook hook);
Status create(
    const char* name,
    Entry entry,
    void* argument,
    ThreadId* id = nullptr);
Status create_for_process(
    const char* name,
    Entry entry,
    void* argument,
    uint64_t process_id,
    uint8_t priority,
    ThreadId* id = nullptr);

// Enters ready kernel threads on their own stacks. Cooperative yield/exit can
// switch directly between threads; control returns to the caller when no
// thread remains ready or the switch budget is exhausted.
Status run_until_idle(
    uint64_t switch_budget,
    RunResult* result = nullptr);
Status yield();
[[noreturn]] void exit_current();

// Runs ready threads from synthetic interrupt frames. IRQ0 can then select a
// different saved frame without cooperation from the running entry function.
Status run_preemptive(PreemptRunResult* result = nullptr);
Status run_preemptive_for(
    uint64_t maximum_timer_ticks,
    PreemptRunResult* result = nullptr);
arch::x86_64::interrupts::InterruptFrame* timer_irq_schedule(
    uint8_t irq,
    arch::x86_64::interrupts::InterruptFrame& frame);

ThreadId current();
uint64_t current_process();
Status bind_address_space(
    memory::kernel_virtual_memory::OwnedAddressSpace* address_space,
    uintptr_t user_stack = 0U);
// Atomically retires the saved resumable Ring-3 frame for the currently
// running process thread. Used when SYS_EXIT commits to kernel teardown so a
// stale user frame can never be selected after its runtime Context is gone.
Status retire_current_user_frame();
Status request_yield();
Status block_current();
Status wake_user(ThreadId id, uint64_t accumulator);
Status sleep_current(uint64_t timer_ticks);
uint64_t timer_ticks();
Status redirect_user(
    ThreadId id,
    uint64_t instruction_pointer,
    uint64_t accumulator,
    uint64_t argument1);
Status stat(ThreadId id, Stat* output);
Status list(ListCallback callback, void* context);
size_t ready_count();
const char* status_message(Status status);

} // namespace threading

extern "C" void x86_64_thread_context_switch(
    uint64_t* previous_stack_pointer,
    const uint64_t* next_stack_pointer);
extern "C" void x86_64_thread_bootstrap();
extern "C" void x86_64_thread_start_interrupt_frame(
    arch::x86_64::interrupts::InterruptFrame* frame);
extern "C" [[noreturn]] void x86_64_thread_resume_interrupt_frame(
    arch::x86_64::interrupts::InterruptFrame* frame);
extern "C" [[noreturn]] void x86_64_thread_return_from_preemptive_run();

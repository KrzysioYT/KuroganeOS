#include "thread.hpp"

#include "../arch/x86_64/gdt.hpp"
#include "../arch/x86_64/interrupts.hpp"
#include "../core/log.hpp"
#if !defined(KUROGANE_HOST_TEST)
#include "../memory/kernel_virtual_memory.hpp"
#endif

extern "C" [[noreturn]] void x86_64_thread_timeout_return();

namespace threading {
namespace {

constexpr size_t kInvalidSlot = static_cast<size_t>(-1);
constexpr uint64_t kSlotMask = UINT64_C(0xFF);

struct Context {
    uint64_t stack_pointer;
};

struct Slot {
    ThreadId id;
    uint64_t generation;
    char name[MAX_THREAD_NAME + 1U];
    Entry entry;
    void* argument;
    State state;
    Context context;
    arch::x86_64::interrupts::InterruptFrame* interrupt_frame;
    uint64_t switches;
    uint64_t process_id;
    uint64_t wake_tick;
    uint64_t address_space_root;
    uintptr_t user_stack;
    uint8_t priority;
    bool started;
    bool yield_requested;
#if !defined(KUROGANE_HOST_TEST)
    memory::kernel_virtual_memory::OwnedAddressSpace* address_space;
#endif
    alignas(16) uint8_t stack[KERNEL_STACK_SIZE];
};

Slot g_slots[MAX_THREADS]{};
Context g_boot_context{};
bool g_initialized = false;
bool g_run_active = false;
size_t g_current = kInvalidSlot;
size_t g_cursor = 0U;
uint64_t g_switch_budget = 0U;
uint64_t g_run_switches = 0U;
uint64_t g_completed_total = 0U;
uint64_t g_run_completed_start = 0U;
bool g_preemptive_active = false;
uint64_t g_preemptions = 0U;
uint64_t g_timer_ticks = 0U;
uint64_t g_preemptive_limit = 0U;
uint64_t g_preemptive_start_tick = 0U;
bool g_preemptive_timed_out = false;
#if !defined(KUROGANE_HOST_TEST)
alignas(16) uint8_t g_timeout_return_stack[4096U]{};
arch::x86_64::interrupts::InterruptFrame g_timeout_return_frame{};
#endif

uint64_t save_and_disable_interrupts() {
#if defined(KUROGANE_HOST_TEST)
    return UINT64_C(0x202);
#else
    uint64_t flags = 0U;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
#endif
}

void restore_interrupts(uint64_t flags) {
#if defined(KUROGANE_HOST_TEST)
    static_cast<void>(flags);
#else
    __asm__ volatile("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
#endif
}

void clear_bytes(void* destination, size_t size) {
    auto* bytes = static_cast<uint8_t*>(destination);
    for (size_t index = 0U; index < size; ++index) {
        bytes[index] = 0U;
    }
}

bool copy_name(char* destination, const char* source) {
    if (source == nullptr || source[0] == '\0') {
        return false;
    }
    for (size_t index = 0U; index <= MAX_THREAD_NAME; ++index) {
        destination[index] = source[index];
        if (source[index] == '\0') {
            return true;
        }
    }
    return false;
}

ThreadId next_id(Slot& slot, size_t index) {
    ++slot.generation;
    if (slot.generation == 0U) {
        slot.generation = 1U;
    }
    return (slot.generation << 8U) | (index + 1U);
}

bool decode_id(ThreadId id, size_t* index) {
    if (id == INVALID_THREAD_ID || index == nullptr) {
        return false;
    }
    const uint64_t encoded = id & kSlotMask;
    if (encoded == 0U || encoded > MAX_THREADS) {
        return false;
    }
    *index = static_cast<size_t>(encoded - 1U);
    return true;
}

size_t find_ready(size_t excluded = kInvalidSlot) {
    for (size_t offset = 0U; offset < MAX_THREADS; ++offset) {
        const size_t index = (g_cursor + offset) % MAX_THREADS;
        if (index != excluded && g_slots[index].state == State::Ready) {
            g_cursor = (index + 1U) % MAX_THREADS;
            return index;
        }
    }
    return kInvalidSlot;
}

void wake_sleepers() {
    for (Slot& slot : g_slots) {
        if (slot.state == State::Sleeping && slot.wake_tick <= g_timer_ticks) {
            slot.wake_tick = 0U;
            slot.state = State::Ready;
        }
    }
}

uintptr_t stack_top(const Slot& slot) {
    return reinterpret_cast<uintptr_t>(slot.stack + KERNEL_STACK_SIZE) &
        ~static_cast<uintptr_t>(0xFU);
}

uintptr_t execution_stack_top(const Slot& slot) {
    return stack_top(slot) - KERNEL_ENTRY_RESERVE;
}

bool activate_slot(size_t index) {
#if defined(KUROGANE_HOST_TEST)
    static_cast<void>(index);
    return true;
#else
    if (index == kInvalidSlot) {
        return memory::kernel_virtual_memory::activate_kernel() ==
                memory::kernel_virtual_memory::Status::Ok &&
            arch::x86_64::gdt::set_kernel_stack(
                arch::x86_64::gdt::kernel_entry_stack_top());
    }
    Slot& slot = g_slots[index];
    const bool address_ok = slot.address_space == nullptr
        ? memory::kernel_virtual_memory::activate_kernel() ==
            memory::kernel_virtual_memory::Status::Ok
        : memory::kernel_virtual_memory::activate(slot.address_space) ==
            memory::kernel_virtual_memory::Status::Ok;
    return address_ok &&
        arch::x86_64::gdt::set_kernel_stack(stack_top(slot));
#endif
}

void initialize_stack(Slot& slot) {
    const uintptr_t top = execution_stack_top(slot);

    // The switch pops r15..rbp and RET. Keep one fake return word so the
    // bootstrap observes the SysV function-entry alignment (RSP % 16 == 8).
    auto* stack = reinterpret_cast<uint64_t*>(top);
    *--stack = 0U;
    *--stack = reinterpret_cast<uint64_t>(&x86_64_thread_bootstrap);
    *--stack = 0U; // rbp
    *--stack = 0U; // rbx
    *--stack = 0U; // r12
    *--stack = 0U; // r13
    *--stack = 0U; // r14
    *--stack = 0U; // r15
    slot.context.stack_pointer = reinterpret_cast<uint64_t>(stack);
}

void initialize_interrupt_frame(Slot& slot) {
    using arch::x86_64::interrupts::InterruptFrame;
    const uintptr_t top = execution_stack_top(slot);
    const uintptr_t target_stack = top - sizeof(uint64_t);
    *reinterpret_cast<uint64_t*>(target_stack) = 0U;
    const uintptr_t frame_address = target_stack - sizeof(InterruptFrame);
    clear_bytes(
        reinterpret_cast<void*>(frame_address),
        sizeof(InterruptFrame));
    auto* frame = reinterpret_cast<InterruptFrame*>(frame_address);
    frame->rip = reinterpret_cast<uint64_t>(&x86_64_thread_bootstrap);
    frame->cs = arch::x86_64::gdt::KERNEL_CODE_SELECTOR;
    frame->rflags = UINT64_C(0x202);
    frame->rsp = target_stack;
    frame->ss = arch::x86_64::gdt::KERNEL_DATA_SELECTOR;
    slot.interrupt_frame = frame;
    slot.started = true;
}

void switch_to(size_t next, bool current_remains_ready) {
    const size_t previous = g_current;
    Slot& old = g_slots[previous];
    Slot& destination = g_slots[next];
    if (current_remains_ready) {
        old.state = State::Ready;
    }
    destination.state = State::Running;
    ++old.switches;
    ++destination.switches;
    ++g_run_switches;
    g_current = next;
    if (!activate_slot(next)) {
        destination.state = State::Terminated;
    }
    x86_64_thread_context_switch(
        &old.context.stack_pointer,
        &destination.context.stack_pointer);
}

void switch_to_boot(bool current_remains_ready) {
    const size_t previous = g_current;
    Slot& old = g_slots[previous];
    if (current_remains_ready) {
        old.state = State::Ready;
    }
    ++old.switches;
    ++g_run_switches;
    g_current = kInvalidSlot;
    static_cast<void>(activate_slot(kInvalidSlot));
    x86_64_thread_context_switch(
        &old.context.stack_pointer,
        &g_boot_context.stack_pointer);
}

void reap_terminated() {
    for (Slot& slot : g_slots) {
        if (slot.state != State::Terminated) {
            continue;
        }
        const uint64_t generation = slot.generation;
        clear_bytes(&slot, sizeof(slot));
        slot.generation = generation;
        slot.state = State::Empty;
    }
}

#if !defined(KUROGANE_HOST_TEST)
arch::x86_64::interrupts::InterruptFrame* prepare_timeout_return(
    size_t previous) {
    Slot& old = g_slots[previous];
    if (old.state == State::Running) {
        old.state = State::Ready;
    }
    g_current = kInvalidSlot;
    g_preemptive_active = false;
    g_preemptive_timed_out = true;
    g_timeout_return_frame = {};
    uintptr_t top = reinterpret_cast<uintptr_t>(
        g_timeout_return_stack + sizeof(g_timeout_return_stack));
    top &= ~static_cast<uintptr_t>(0xFU);
    const uintptr_t target_stack = top - sizeof(uint64_t);
    *reinterpret_cast<uint64_t*>(target_stack) = 0U;
    g_timeout_return_frame.rip = reinterpret_cast<uint64_t>(
        &x86_64_thread_timeout_return);
    g_timeout_return_frame.cs = arch::x86_64::gdt::KERNEL_CODE_SELECTOR;
    g_timeout_return_frame.rflags = UINT64_C(0x2);
    g_timeout_return_frame.rsp = target_stack;
    g_timeout_return_frame.ss = arch::x86_64::gdt::KERNEL_DATA_SELECTOR;
    static_cast<void>(activate_slot(kInvalidSlot));
    return &g_timeout_return_frame;
}
#endif

arch::x86_64::interrupts::InterruptFrame* software_interrupt_schedule(
    uint8_t vector,
    arch::x86_64::interrupts::InterruptFrame& frame) {
    static_cast<void>(vector);
    if (!g_preemptive_active || !g_run_active ||
        g_current == kInvalidSlot || (frame.cs & 3U) != 3U) {
        return &frame;
    }

    const size_t previous = g_current;
    Slot& old = g_slots[previous];
    if (old.process_id == 0U) {
        return &frame;
    }

#if !defined(KUROGANE_HOST_TEST)
    // The syscall gate is a trap gate, so IF may still be set. Publish and
    // select frames with interrupts disabled; the chosen IRET frame restores
    // the destination's interrupt state atomically.
    __asm__ volatile("cli" : : : "memory");
#endif

    // A software interrupt from Ring-3 is the only point where the complete
    // user return frame is guaranteed to be available while the syscall may
    // have changed the thread state to Sleeping/Blocked or requested yield.
    old.interrupt_frame = &frame;
    const bool yield_requested = old.yield_requested;
    old.yield_requested = false;
    if (yield_requested && old.state == State::Running) {
        old.state = State::Ready;
    }
    if (!yield_requested && old.state == State::Running) {
        return &frame;
    }

    for (;;) {
#if !defined(KUROGANE_HOST_TEST)
        if (g_preemptive_limit != 0U &&
            g_timer_ticks - g_preemptive_start_tick >= g_preemptive_limit) {
            return prepare_timeout_return(previous);
        }
#endif

        const size_t next = find_ready(previous);
        if (next != kInvalidSlot) {
            auto* next_frame = g_slots[next].interrupt_frame;
            if (next_frame == nullptr) {
                g_slots[next].state = State::Terminated;
                continue;
            }
            const State old_state = old.state;
            g_slots[next].state = State::Running;
            ++old.switches;
            ++g_slots[next].switches;
            g_current = next;
            if (!activate_slot(next)) {
                g_current = previous;
                g_slots[next].state = State::Ready;
                old.state = old_state;
                static_cast<void>(activate_slot(previous));
                if (old.state == State::Ready) {
                    old.state = State::Running;
                    return &frame;
                }
                continue;
            }
            return next_frame;
        }

        // PIT IRQs that interrupt this kernel-side scheduling boundary only
        // advance time and wake sleepers; they never replace the saved user
        // frame. Once this thread wakes, resume exactly the syscall frame that
        // entered here.
        if (old.state == State::Ready) {
            old.state = State::Running;
            old.wake_tick = 0U;
            g_current = previous;
            static_cast<void>(activate_slot(previous));
            return &frame;
        }
        if (old.state == State::Running) {
            return &frame;
        }
        if (old.state != State::Sleeping && old.state != State::Blocked) {
            old.state = State::Running;
            g_current = previous;
            static_cast<void>(activate_slot(previous));
            return &frame;
        }

#if defined(KUROGANE_HOST_TEST)
        return &frame;
#else
        __asm__ volatile("sti; hlt; cli" : : : "memory");
#endif
    }
}

} // namespace

Status initialize() {
    const uint64_t flags = save_and_disable_interrupts();
    if (g_initialized) {
        restore_interrupts(flags);
        return Status::AlreadyInitialized;
    }
    clear_bytes(g_slots, sizeof(g_slots));
    g_boot_context = {};
    g_current = kInvalidSlot;
    g_cursor = 0U;
    g_switch_budget = 0U;
    g_run_switches = 0U;
    g_completed_total = 0U;
    g_run_active = false;
    g_preemptive_active = false;
    g_preemptions = 0U;
    g_timer_ticks = 0U;
    g_preemptive_limit = 0U;
    g_preemptive_start_tick = 0U;
    g_preemptive_timed_out = false;
#if !defined(KUROGANE_HOST_TEST)
    if (!arch::x86_64::interrupts::register_software_schedule_hook(
            software_interrupt_schedule)) {
        restore_interrupts(flags);
        return Status::CorruptContext;
    }
#endif
    g_initialized = true;
    restore_interrupts(flags);
    return Status::Ok;
}

Status create(
    const char* name,
    Entry entry,
    void* argument,
    ThreadId* id) {
    return create_for_process(name, entry, argument, 0U, 0U, id);
}

Status create_for_process(
    const char* name,
    Entry entry,
    void* argument,
    uint64_t process_id,
    uint8_t priority,
    ThreadId* id) {
    if (id != nullptr) {
        *id = INVALID_THREAD_ID;
    }
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    if (entry == nullptr || name == nullptr || name[0] == '\0') {
        return Status::InvalidArgument;
    }

    const uint64_t flags = save_and_disable_interrupts();
    size_t index = kInvalidSlot;
    for (size_t candidate = 0U; candidate < MAX_THREADS; ++candidate) {
        if (g_slots[candidate].state == State::Empty) {
            index = candidate;
            break;
        }
    }
    if (index == kInvalidSlot) {
        restore_interrupts(flags);
        return Status::CapacityReached;
    }

    Slot& slot = g_slots[index];
    const uint64_t generation = slot.generation;
    clear_bytes(&slot, sizeof(slot));
    slot.generation = generation;
    if (!copy_name(slot.name, name)) {
        clear_bytes(&slot, sizeof(slot));
        slot.generation = generation;
        restore_interrupts(flags);
        return Status::NameTooLong;
    }
    slot.id = next_id(slot, index);
    slot.entry = entry;
    slot.argument = argument;
    slot.process_id = process_id;
    slot.priority = priority;
    slot.state = State::Ready;
    initialize_stack(slot);
    if (g_preemptive_active) {
        initialize_interrupt_frame(slot);
    }
    if (id != nullptr) {
        *id = slot.id;
    }
    restore_interrupts(flags);
    return Status::Ok;
}

Status run_until_idle(uint64_t switch_budget, RunResult* result) {
    if (result != nullptr) {
        *result = {};
    }
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    if (switch_budget == 0U) {
        return Status::InvalidArgument;
    }

    const uint64_t flags = save_and_disable_interrupts();
    if (g_run_active || g_current != kInvalidSlot) {
        restore_interrupts(flags);
        return Status::Busy;
    }
    reap_terminated();
    const size_t next = find_ready();
    if (next == kInvalidSlot) {
        restore_interrupts(flags);
        return Status::Ok;
    }
    g_run_active = true;
    g_switch_budget = switch_budget;
    g_run_switches = 1U;
    g_run_completed_start = g_completed_total;
    g_slots[next].state = State::Running;
    ++g_slots[next].switches;
    g_current = next;
    restore_interrupts(flags);

    x86_64_thread_context_switch(
        &g_boot_context.stack_pointer,
        &g_slots[next].context.stack_pointer);

    const uint64_t finish_flags = save_and_disable_interrupts();
    g_run_active = false;
    const size_t remaining = ready_count();
    const uint64_t completed = g_completed_total - g_run_completed_start;
    const uint64_t switches = g_run_switches;
    reap_terminated();
    restore_interrupts(finish_flags);
    if (result != nullptr) {
        result->switches = switches;
        result->completed = completed;
        result->ready_remaining = remaining;
    }
    return remaining == 0U ? Status::Ok : Status::BudgetExhausted;
}

Status yield() {
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    if (g_current == kInvalidSlot || !g_run_active) {
        return Status::NotRunning;
    }
    if (g_preemptive_active) {
        // Ring-3 KU_SYS_YIELD records a request that is consumed by the
        // post-software-interrupt scheduler. Direct kernel callers remain a
        // safe no-op while timer preemption continues.
        return Status::Ok;
    }
    const uint64_t flags = save_and_disable_interrupts();
    const size_t old = g_current;
    if (g_run_switches >= g_switch_budget) {
        restore_interrupts(flags);
        switch_to_boot(true);
        return Status::Ok;
    }
    const size_t next = find_ready(old);
    if (next == kInvalidSlot) {
        restore_interrupts(flags);
        return Status::Ok;
    }
    restore_interrupts(flags);
    switch_to(next, true);
    return Status::Ok;
}

[[noreturn]] void exit_current() {
    if (!g_initialized || g_current == kInvalidSlot || !g_run_active) {
        for (;;) {
#if defined(KUROGANE_HOST_TEST)
            __builtin_trap();
#else
            __asm__ volatile("cli; hlt");
#endif
        }
    }

    const uint64_t flags = save_and_disable_interrupts();
    const size_t old = g_current;
    g_slots[old].state = State::Terminated;
    ++g_completed_total;
    if (g_preemptive_active) {
        const size_t next = find_ready(old);
        if (next == kInvalidSlot) {
            g_current = kInvalidSlot;
            g_preemptive_active = false;
            static_cast<void>(activate_slot(kInvalidSlot));
            static_cast<void>(flags);
            x86_64_thread_return_from_preemptive_run();
        }
        g_slots[next].state = State::Running;
        ++g_slots[next].switches;
        g_current = next;
        if (!activate_slot(next)) {
            g_slots[next].state = State::Terminated;
            g_current = kInvalidSlot;
            g_preemptive_active = false;
            static_cast<void>(activate_slot(kInvalidSlot));
            x86_64_thread_return_from_preemptive_run();
        }
        static_cast<void>(flags);
        x86_64_thread_resume_interrupt_frame(
            g_slots[next].interrupt_frame);
    }
    const size_t next =
        g_run_switches < g_switch_budget ? find_ready(old) : kInvalidSlot;
    restore_interrupts(flags);
    if (next == kInvalidSlot) {
        switch_to_boot(false);
    } else {
        switch_to(next, false);
    }
    __builtin_unreachable();
}

Status run_preemptive(PreemptRunResult* result) {
    return run_preemptive_for(0U, result);
}

Status run_preemptive_for(
    uint64_t maximum_timer_ticks,
    PreemptRunResult* result) {
    if (result != nullptr) {
        *result = {};
    }
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    const uint64_t flags = save_and_disable_interrupts();
    if (g_run_active || g_current != kInvalidSlot || g_preemptive_active) {
        restore_interrupts(flags);
        return Status::Busy;
    }
    reap_terminated();
    const size_t first = find_ready();
    if (first == kInvalidSlot) {
        restore_interrupts(flags);
        return Status::Ok;
    }
    for (Slot& slot : g_slots) {
        if (slot.state == State::Ready && !slot.started) {
            initialize_interrupt_frame(slot);
        }
    }
    g_run_active = true;
    g_preemptive_active = true;
    g_preemptions = 0U;
    g_preemptive_limit = maximum_timer_ticks;
    g_preemptive_start_tick = g_timer_ticks;
    g_preemptive_timed_out = false;
    g_run_completed_start = g_completed_total;
    g_slots[first].state = State::Running;
    ++g_slots[first].switches;
    g_current = first;
    if (!activate_slot(first)) {
        g_current = kInvalidSlot;
        g_run_active = false;
        g_preemptive_active = false;
        restore_interrupts(flags);
        return Status::CorruptContext;
    }

    // Keep interrupts disabled between publishing scheduler state and the
    // synthetic IRET. The new frame enables IF atomically at its entry point.
    x86_64_thread_start_interrupt_frame(g_slots[first].interrupt_frame);

    const uint64_t completed = g_completed_total - g_run_completed_start;
    const uint64_t preemptions = g_preemptions;
    const uint64_t elapsed_ticks = g_timer_ticks - g_preemptive_start_tick;
    const bool timed_out = g_preemptive_timed_out;
    g_run_active = false;
    g_preemptive_limit = 0U;
    reap_terminated();
    restore_interrupts(flags);
    if (result != nullptr) {
        result->completed = completed;
        result->preemptions = preemptions;
        result->timer_ticks = elapsed_ticks;
        result->timed_out = timed_out;
    }
    return timed_out || ready_count() == 0U
        ? Status::Ok
        : Status::CorruptContext;
}

arch::x86_64::interrupts::InterruptFrame* timer_irq_schedule(
    uint8_t irq,
    arch::x86_64::interrupts::InterruptFrame& frame) {
    if (irq != 0U) {
        return &frame;
    }
    ++g_timer_ticks;
    wake_sleepers();
    if (!g_preemptive_active || !g_run_active ||
        g_current == kInvalidSlot) {
        return &frame;
    }
    const size_t previous = g_current;
    Slot& old = g_slots[previous];

    // IRQ0 may nest while a Ring-3 process is still executing its syscall
    // handler or the software-schedule hook. A same-CPL kernel IRQ does not
    // contain the SS:RSP half of a resumable user frame, so it may advance
    // time and wake sleepers but must never replace/schedule that process'
    // saved Ring-3 frame. Kernel threads (process_id == 0) retain normal IRQ
    // preemption, preserving the kernel preemption qualification.
    if (old.process_id != 0U && (frame.cs & 3U) == 0U) {
        return &frame;
    }

    old.interrupt_frame = &frame;

    if (g_preemptive_limit != 0U &&
        g_timer_ticks - g_preemptive_start_tick >= g_preemptive_limit) {
        if (old.state == State::Running) {
            old.state = State::Ready;
        }
        g_current = kInvalidSlot;
        g_preemptive_active = false;
        g_preemptive_timed_out = true;
#if !defined(KUROGANE_HOST_TEST)
        g_timeout_return_frame = {};
        uintptr_t top = reinterpret_cast<uintptr_t>(
            g_timeout_return_stack + sizeof(g_timeout_return_stack));
        top &= ~static_cast<uintptr_t>(0xFU);
        const uintptr_t target_stack = top - sizeof(uint64_t);
        *reinterpret_cast<uint64_t*>(target_stack) = 0U;
        g_timeout_return_frame.rip = reinterpret_cast<uint64_t>(
            &x86_64_thread_timeout_return);
        g_timeout_return_frame.cs =
            arch::x86_64::gdt::KERNEL_CODE_SELECTOR;
        g_timeout_return_frame.rflags = UINT64_C(0x2);
        g_timeout_return_frame.rsp = target_stack;
        g_timeout_return_frame.ss =
            arch::x86_64::gdt::KERNEL_DATA_SELECTOR;
        static_cast<void>(activate_slot(kInvalidSlot));
        return &g_timeout_return_frame;
#else
        return &frame;
#endif
    }

    const size_t next = find_ready(previous);
    if (next == kInvalidSlot) {
        if (old.state == State::Sleeping) {
            old.yield_requested = false;
            return &frame;
        }
        if (old.state != State::Running) {
            old.state = State::Running;
            old.wake_tick = 0U;
        }
        old.yield_requested = false;
        return &frame;
    }
    if (old.state == State::Running) {
        old.state = State::Ready;
    }
    old.yield_requested = false;
    g_slots[next].state = State::Running;
    ++old.switches;
    ++g_slots[next].switches;
    ++g_preemptions;
    g_current = next;
    if (!activate_slot(next)) {
        g_current = previous;
        old.state = State::Running;
        g_slots[next].state = State::Ready;
        static_cast<void>(activate_slot(previous));
        return &frame;
    }
    return g_slots[next].interrupt_frame;
}

ThreadId current() {
    return g_current == kInvalidSlot
        ? INVALID_THREAD_ID
        : g_slots[g_current].id;
}

uint64_t current_process() {
    return g_current == kInvalidSlot
        ? 0U
        : g_slots[g_current].process_id;
}

Status retire_current_user_frame() {
    if (!g_initialized) return Status::NotInitialized;
    const uint64_t flags = save_and_disable_interrupts();
    if (g_current == kInvalidSlot || !g_run_active) {
        restore_interrupts(flags);
        return Status::NotRunning;
    }
    Slot& slot = g_slots[g_current];
    if (slot.process_id == 0U || slot.state != State::Running) {
        restore_interrupts(flags);
        return Status::CorruptContext;
    }

    // Once userspace has committed to SYS_EXIT, no scheduler path may retain
    // a resumable frame for that address space. The current interrupt frame
    // is rewritten to a CPL0 trampoline by runtime::finish_from_interrupt;
    // this clears only the older scheduler-owned resume pointer.
    slot.interrupt_frame = nullptr;
    slot.wake_tick = 0U;
    slot.yield_requested = false;
    restore_interrupts(flags);
    return Status::Ok;
}

Status bind_address_space(
    memory::kernel_virtual_memory::OwnedAddressSpace* address_space,
    uintptr_t user_stack) {
    if (!g_initialized) return Status::NotInitialized;
    if (g_current == kInvalidSlot) return Status::NotRunning;
    Slot& slot = g_slots[g_current];
#if defined(KUROGANE_HOST_TEST)
    static_cast<void>(address_space);
    slot.address_space_root = 0U;
#else
    slot.address_space = address_space;
    slot.address_space_root = address_space == nullptr
        ? memory::kernel_virtual_memory::root_table_physical()
        : address_space->address_space.root_table_physical;
#endif
    slot.user_stack = user_stack;
    return Status::Ok;
}

Status request_yield() {
    if (!g_initialized) return Status::NotInitialized;
    if (g_current == kInvalidSlot || !g_run_active) {
        return Status::NotRunning;
    }
    Slot& slot = g_slots[g_current];
    slot.yield_requested = true;
    return Status::Ok;
}

Status sleep_current(uint64_t timer_count) {
    if (!g_initialized) return Status::NotInitialized;
    if (g_current == kInvalidSlot || !g_run_active) {
        return Status::NotRunning;
    }
    if (timer_count == 0U || timer_count > UINT64_MAX - g_timer_ticks) {
        return Status::InvalidArgument;
    }

    Slot& slot = g_slots[g_current];
    slot.wake_tick = g_timer_ticks + timer_count;
    slot.state = State::Sleeping;

    // Do not schedule from inside the syscall handler. The post-software-
    // interrupt hook owns the complete Ring-3 return frame and either switches
    // to a peer or waits for IRQ0 to wake this slot before returning to user.
    return Status::Ok;
}

uint64_t timer_ticks() {
    return g_timer_ticks;
}

Status redirect_user(
    ThreadId id,
    uint64_t instruction_pointer,
    uint64_t accumulator,
    uint64_t argument1) {
    if (!g_initialized) return Status::NotInitialized;
    size_t index = 0U;
    if (!decode_id(id, &index)) return Status::NotFound;
    Slot& slot = g_slots[index];
    if (slot.state == State::Empty || slot.state == State::Terminated ||
        slot.id != id || slot.interrupt_frame == nullptr ||
        (slot.interrupt_frame->cs & 3U) != 3U) {
        return Status::NotFound;
    }
    slot.interrupt_frame->rip = instruction_pointer;
    slot.interrupt_frame->rax = accumulator;
    slot.interrupt_frame->rdi = argument1;
    slot.state = State::Ready;
    slot.wake_tick = 0U;
    return Status::Ok;
}

Status stat(ThreadId id, Stat* output) {
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    if (output == nullptr) {
        return Status::InvalidArgument;
    }
    *output = {};
    size_t index = 0U;
    if (!decode_id(id, &index)) {
        return Status::NotFound;
    }
    const Slot& slot = g_slots[index];
    if (slot.state == State::Empty || slot.id != id) {
        return Status::NotFound;
    }
    output->id = slot.id;
    output->state = slot.state;
    output->switches = slot.switches;
    output->stack_bottom = reinterpret_cast<uintptr_t>(slot.stack);
    output->stack_top = reinterpret_cast<uintptr_t>(
        slot.stack + KERNEL_STACK_SIZE);
    output->user_stack = slot.user_stack;
    output->process_id = slot.process_id;
    output->address_space_root = slot.address_space_root;
    output->wake_tick = slot.wake_tick;
    output->priority = slot.priority;
    for (size_t name_index = 0U; name_index <= MAX_THREAD_NAME; ++name_index) {
        output->name[name_index] = slot.name[name_index];
        if (slot.name[name_index] == '\0') {
            break;
        }
    }
    return Status::Ok;
}

Status list(ListCallback callback, void* context) {
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    if (callback == nullptr) {
        return Status::InvalidArgument;
    }
    for (const Slot& slot : g_slots) {
        if (slot.state == State::Empty) {
            continue;
        }
        Stat snapshot{};
        snapshot.id = slot.id;
        snapshot.state = slot.state;
        snapshot.switches = slot.switches;
        snapshot.stack_bottom = reinterpret_cast<uintptr_t>(slot.stack);
        snapshot.stack_top = reinterpret_cast<uintptr_t>(
            slot.stack + KERNEL_STACK_SIZE);
        snapshot.user_stack = slot.user_stack;
        snapshot.process_id = slot.process_id;
        snapshot.address_space_root = slot.address_space_root;
        snapshot.wake_tick = slot.wake_tick;
        snapshot.priority = slot.priority;
        for (size_t index = 0U; index <= MAX_THREAD_NAME; ++index) {
            snapshot.name[index] = slot.name[index];
            if (snapshot.name[index] == '\0') {
                break;
            }
        }
        if (!callback(snapshot, context)) {
            break;
        }
    }
    return Status::Ok;
}

size_t ready_count() {
    size_t count = 0U;
    for (const Slot& slot : g_slots) {
        if (slot.state == State::Ready) {
            ++count;
        }
    }
    return count;
}

const char* status_message(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::NotInitialized: return "not initialized";
        case Status::AlreadyInitialized: return "already initialized";
        case Status::InvalidArgument: return "invalid argument";
        case Status::NameTooLong: return "name too long";
        case Status::CapacityReached: return "thread capacity reached";
        case Status::NotFound: return "thread not found";
        case Status::NotRunning: return "no kernel thread is running";
        case Status::Busy: return "thread runner is busy";
        case Status::BudgetExhausted: return "switch budget exhausted";
        case Status::CorruptContext: return "corrupt thread context";
    }
    return "unknown thread status";
}

} // namespace threading

extern "C" void x86_64_thread_bootstrap() {
    const threading::ThreadId id = threading::current();
    threading::Stat snapshot{};
    if (id == threading::INVALID_THREAD_ID ||
        threading::stat(id, &snapshot) != threading::Status::Ok) {
        threading::exit_current();
    }

    // The private slot is intentionally reached only through the public ID in
    // this translation unit; retrieve entry/argument via a bounded lookup.
    size_t index = static_cast<size_t>((id & UINT64_C(0xFF)) - 1U);
    auto& slot = threading::g_slots[index];
    threading::Entry entry = slot.entry;
    void* argument = slot.argument;
    if (entry != nullptr) {
        entry(argument);
    }
    threading::exit_current();
}

extern "C" [[noreturn]] void x86_64_thread_timeout_return() {
#if defined(KUROGANE_HOST_TEST)
    __builtin_trap();
#else
    x86_64_thread_return_from_preemptive_run();
#endif
}

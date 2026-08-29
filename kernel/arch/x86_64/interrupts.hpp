#pragma once

#include <stddef.h>
#include <stdint.h>

namespace arch::x86_64::interrupts {

constexpr size_t IDT_ENTRY_COUNT = 256;
constexpr uint8_t IRQ_VECTOR_BASE = 0x20;
constexpr uint8_t IRQ_COUNT = 16;

// Layout produced by interrupt_stubs.asm. Ring-3 entries include the complete
// SS:RSP/RFLAGS:CS:RIP return state. Same-CPL kernel IRQs contain only the
// hardware frame required by IRETQ for that privilege level; scheduler code
// must therefore never treat a kernel-interrupted frame as a resumable user
// context.
struct InterruptFrame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

static_assert(
    offsetof(InterruptFrame, vector) == 15 * sizeof(uint64_t),
    "assembly/C++ interrupt-frame register layout mismatch");
static_assert(
    offsetof(InterruptFrame, error_code) == 16 * sizeof(uint64_t),
    "assembly/C++ interrupt-frame error-code layout mismatch");
static_assert(
    offsetof(InterruptFrame, rip) == 17 * sizeof(uint64_t),
    "assembly/C++ interrupt-frame RIP layout mismatch");
static_assert(
    offsetof(InterruptFrame, cs) == 18 * sizeof(uint64_t),
    "assembly/C++ interrupt-frame CS layout mismatch");
static_assert(
    offsetof(InterruptFrame, rflags) == 19 * sizeof(uint64_t),
    "assembly/C++ interrupt-frame RFLAGS layout mismatch");
static_assert(
    offsetof(InterruptFrame, rsp) == 20 * sizeof(uint64_t),
    "assembly/C++ interrupt-frame RSP layout mismatch");
static_assert(
    offsetof(InterruptFrame, ss) == 21 * sizeof(uint64_t),
    "assembly/C++ interrupt-frame SS layout mismatch");
static_assert(
    sizeof(InterruptFrame) == 22 * sizeof(uint64_t),
    "assembly/C++ interrupt-frame size mismatch");

using InterruptHandler = void (*)(InterruptFrame& frame);
using IrqHandler = void (*)();
using IrqScheduleHook = InterruptFrame* (*)(
    uint8_t irq,
    InterruptFrame& frame);
using SoftwareScheduleHook = InterruptFrame* (*)(
    uint8_t vector,
    InterruptFrame& frame);

enum class GatePrivilege : uint8_t {
    Kernel = 0,
    User = 3,
};

enum class GateType : uint8_t {
    Interrupt = 0,
    Trap
};

// Loads a complete 256-entry IDT and leaves maskable interrupts disabled.
void initialize();
bool initialized();

bool register_handler(uint8_t vector, InterruptHandler handler);
void unregister_handler(uint8_t vector);

// A ring-3 callable gate is permitted only for a software-defined vector that
// already has a handler. Removing that handler first demotes the gate to ring
// 0, so this API cannot leave an unhandled user-callable gate behind.
bool set_gate_privilege(uint8_t vector, GatePrivilege privilege);
// Trap gates preserve IF and are suitable for preemptible software syscalls.
// Exception and hardware IRQ gates remain interrupt gates.
bool set_gate_type(uint8_t vector, GateType type);

bool register_irq_handler(uint8_t irq, IrqHandler handler);
void unregister_irq_handler(uint8_t irq);

// Installs the single low-level scheduling hook invoked after an IRQ handler
// and EOI. Returning a different complete interrupt frame performs the stack
// switch in the common assembly epilogue. Intended for the thread scheduler.
bool register_irq_schedule_hook(IrqScheduleHook hook);
void unregister_irq_schedule_hook(IrqScheduleHook hook);

// Installs a post-handler scheduler hook for software-defined interrupt gates.
// This is the safe boundary for a blocking/yielding Ring-3 syscall: the frame
// still contains the complete user return state, so the scheduler may save it
// and return another process' frame without ever switching from a nested
// kernel IRQ frame.
bool register_software_schedule_hook(SoftwareScheduleHook hook);
void unregister_software_schedule_hook(SoftwareScheduleHook hook);

void enable();
void disable();
bool enabled();

uint64_t interrupt_count(uint8_t vector);
uint8_t last_exception_vector();
uint64_t last_exception_error_code();
uintptr_t last_page_fault_address();

[[noreturn]] void halt();

} // namespace arch::x86_64::interrupts

extern "C" arch::x86_64::interrupts::InterruptFrame*
x86_64_interrupt_dispatch(
    arch::x86_64::interrupts::InterruptFrame* frame);

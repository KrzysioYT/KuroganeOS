#pragma once

#include <stddef.h>
#include <stdint.h>

namespace arch::x86_64::interrupts {

constexpr size_t IDT_ENTRY_COUNT = 256;
constexpr uint8_t IRQ_VECTOR_BASE = 0x20;
constexpr uint8_t IRQ_COUNT = 16;

// Layout produced by interrupt_stubs.asm. rsp and ss are present only when the
// processor changed privilege level while entering the interrupt.
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

using InterruptHandler = void (*)(InterruptFrame& frame);
using IrqHandler = void (*)();

// Loads a complete 256-entry IDT and leaves maskable interrupts disabled.
void initialize();
bool initialized();

bool register_handler(uint8_t vector, InterruptHandler handler);
void unregister_handler(uint8_t vector);
bool register_irq_handler(uint8_t irq, IrqHandler handler);
void unregister_irq_handler(uint8_t irq);

void enable();
void disable();
bool enabled();

uint64_t interrupt_count(uint8_t vector);
uint8_t last_exception_vector();
uint64_t last_exception_error_code();
uintptr_t last_page_fault_address();

[[noreturn]] void halt();

} // namespace arch::x86_64::interrupts

extern "C" void x86_64_interrupt_dispatch(
    arch::x86_64::interrupts::InterruptFrame* frame);

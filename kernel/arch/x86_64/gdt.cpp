#include "gdt.hpp"

#include <stddef.h>

namespace arch::x86_64::gdt {

namespace {

struct [[gnu::packed]] GdtRegister {
    uint16_t limit;
    uint64_t base;
};

struct [[gnu::packed]] TaskStateSegment {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
};

static_assert(sizeof(GdtRegister) == 10,
              "x86-64 GDTR operand must be 10 bytes");
static_assert(sizeof(TaskStateSegment) == 104,
              "x86-64 TSS must be 104 bytes without an I/O bitmap");

constexpr size_t kEmergencyStackSize = 16 * 1024;
static_assert(
    (KERNEL_ENTRY_STACK_SIZE & UINT64_C(0xF)) == 0,
    "kernel entry stack size must preserve ABI alignment");
alignas(16) uint8_t g_kernel_entry_stack[KERNEL_ENTRY_STACK_SIZE];
alignas(16) uint8_t g_double_fault_stack[kEmergencyStackSize];
alignas(16) uint8_t g_nmi_stack[kEmergencyStackSize];
alignas(16) uint8_t g_machine_check_stack[kEmergencyStackSize];
alignas(16) TaskStateSegment g_tss{};

// Null, ring-0 code/data, ring-3 data/code and a two-slot 64-bit TSS.
// Ring 3 is not entered yet; stable selectors avoid a later ABI change.
alignas(16) uint64_t g_table[] = {
    UINT64_C(0x0000000000000000),
    UINT64_C(0x00AF9A000000FFFF),
    UINT64_C(0x00CF92000000FFFF),
    UINT64_C(0x00CFF2000000FFFF),
    UINT64_C(0x00AFFA000000FFFF),
    UINT64_C(0x0000000000000000),
    UINT64_C(0x0000000000000000),
};

bool g_initialized = false;

bool is_canonical(uintptr_t address) {
    const uint64_t upper = static_cast<uint64_t>(address) >> 47;
    return upper == 0 || upper == UINT64_C(0x1FFFF);
}

uintptr_t emergency_stack_top(uint8_t* stack) {
    return reinterpret_cast<uintptr_t>(stack + kEmergencyStackSize);
}

void install_tss_descriptor() {
    const uint64_t base = reinterpret_cast<uint64_t>(&g_tss);
    const uint64_t limit = sizeof(g_tss) - 1;
    g_table[5] =
        (limit & UINT64_C(0xFFFF)) |
        ((base & UINT64_C(0xFFFFFF)) << 16) |
        (UINT64_C(0x89) << 40) |
        (((limit >> 16) & UINT64_C(0xF)) << 48) |
        (((base >> 24) & UINT64_C(0xFF)) << 56);
    g_table[6] = base >> 32;
}

} // namespace

void initialize() {
    g_tss = {};
    g_tss.rsp0 = kernel_entry_stack_top();
    g_tss.ist1 = emergency_stack_top(g_double_fault_stack);
    g_tss.ist2 = emergency_stack_top(g_nmi_stack);
    g_tss.ist3 = emergency_stack_top(g_machine_check_stack);
    g_tss.io_map_base = sizeof(g_tss);
    install_tss_descriptor();

    const GdtRegister descriptor = {
        static_cast<uint16_t>(sizeof(g_table) - 1),
        reinterpret_cast<uint64_t>(&g_table[0]),
    };

    __asm__ volatile(
        "cli\n\t"
        "lgdt %0\n\t"
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "xorw %%ax, %%ax\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movw $0x28, %%ax\n\t"
        "ltr %%ax\n\t"
        :
        : "m"(descriptor)
        : "rax", "memory");

    g_initialized = true;
}

bool initialized() {
    return g_initialized;
}

uintptr_t kernel_entry_stack_top() {
    return reinterpret_cast<uintptr_t>(
        g_kernel_entry_stack + KERNEL_ENTRY_STACK_SIZE);
}

bool set_kernel_stack(uintptr_t stack_top) {
    if (!g_initialized || stack_top == 0 || !is_canonical(stack_top) ||
        (stack_top & UINT64_C(0xF)) != 0) {
        return false;
    }

    g_tss.rsp0 = stack_top;
    return true;
}

uintptr_t rsp0() {
    return static_cast<uintptr_t>(g_tss.rsp0);
}

} // namespace arch::x86_64::gdt

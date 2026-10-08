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
constexpr size_t kDescriptorCount = 7U;
static_assert((KERNEL_ENTRY_STACK_SIZE & UINT64_C(0xF)) == 0);

alignas(64) uint8_t
    g_kernel_entry_stacks[MAX_CPU_GDT_STATES][KERNEL_ENTRY_STACK_SIZE]{};
alignas(64) uint8_t
    g_double_fault_stacks[MAX_CPU_GDT_STATES][kEmergencyStackSize]{};
alignas(64) uint8_t
    g_nmi_stacks[MAX_CPU_GDT_STATES][kEmergencyStackSize]{};
alignas(64) uint8_t
    g_machine_check_stacks[MAX_CPU_GDT_STATES][kEmergencyStackSize]{};
alignas(64) TaskStateSegment g_tss[MAX_CPU_GDT_STATES]{};
alignas(64) uint64_t g_tables[MAX_CPU_GDT_STATES][kDescriptorCount]{};
alignas(64) uint8_t g_loaded[MAX_CPU_GDT_STATES]{};
size_t g_loaded_count = 0U;
bool g_initialized = false;

bool is_canonical(uintptr_t address) {
    const uint64_t upper = static_cast<uint64_t>(address) >> 47U;
    return upper == 0U || upper == UINT64_C(0x1FFFF);
}

uintptr_t stack_top(uint8_t* stack, size_t size) {
    return reinterpret_cast<uintptr_t>(stack + size);
}

void seed_table(size_t cpu_index) {
    uint64_t* table = g_tables[cpu_index];
    table[0] = UINT64_C(0x0000000000000000);
    table[1] = UINT64_C(0x00AF9A000000FFFF);
    table[2] = UINT64_C(0x00CF92000000FFFF);
    table[3] = UINT64_C(0x00CFF2000000FFFF);
    table[4] = UINT64_C(0x00AFFA000000FFFF);
    table[5] = UINT64_C(0);
    table[6] = UINT64_C(0);
}

void install_tss_descriptor(size_t cpu_index) {
    const uint64_t base = reinterpret_cast<uint64_t>(&g_tss[cpu_index]);
    const uint64_t limit = sizeof(TaskStateSegment) - 1U;
    uint64_t* table = g_tables[cpu_index];
    table[5] =
        (limit & UINT64_C(0xFFFF)) |
        ((base & UINT64_C(0xFFFFFF)) << 16U) |
        (UINT64_C(0x89) << 40U) |
        (((limit >> 16U) & UINT64_C(0xF)) << 48U) |
        (((base >> 24U) & UINT64_C(0xFF)) << 56U);
    table[6] = base >> 32U;
}

void prepare_cpu(size_t cpu_index) {
    TaskStateSegment& tss = g_tss[cpu_index];
    tss = {};
    tss.rsp0 = kernel_entry_stack_top_for_cpu(cpu_index);
    tss.ist1 = stack_top(g_double_fault_stacks[cpu_index], kEmergencyStackSize);
    tss.ist2 = stack_top(g_nmi_stacks[cpu_index], kEmergencyStackSize);
    tss.ist3 = stack_top(g_machine_check_stacks[cpu_index], kEmergencyStackSize);
    tss.io_map_base = sizeof(TaskStateSegment);
    seed_table(cpu_index);
    install_tss_descriptor(cpu_index);
}

void load_table(size_t cpu_index) {
    const GdtRegister descriptor{
        static_cast<uint16_t>(sizeof(g_tables[cpu_index]) - 1U),
        reinterpret_cast<uint64_t>(&g_tables[cpu_index][0]),
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
}

} // namespace

void initialize() {
    if (g_initialized) return;
    prepare_cpu(0U);
    load_table(0U);
    __atomic_store_n(&g_loaded[0], uint8_t{1U}, __ATOMIC_RELEASE);
    __atomic_store_n(&g_loaded_count, size_t{1U}, __ATOMIC_RELEASE);
    __atomic_store_n(&g_initialized, true, __ATOMIC_RELEASE);
}

bool initialized() {
    return __atomic_load_n(&g_initialized, __ATOMIC_ACQUIRE);
}

bool load_current_cpu(size_t cpu_index) {
    if (!initialized() || cpu_index >= MAX_CPU_GDT_STATES) return false;
    if (__atomic_load_n(&g_loaded[cpu_index], __ATOMIC_ACQUIRE) != 0U) {
        // Each logical CPU calls this once. Reloading a busy TSS selector would
        // raise #GP, so a repeated call is intentionally treated as success.
        return true;
    }
    prepare_cpu(cpu_index);
    load_table(cpu_index);
    uint8_t expected = 0U;
    if (__atomic_compare_exchange_n(
            &g_loaded[cpu_index],
            &expected,
            uint8_t{1U},
            false,
            __ATOMIC_RELEASE,
            __ATOMIC_RELAXED)) {
        __atomic_fetch_add(&g_loaded_count, size_t{1U}, __ATOMIC_ACQ_REL);
    }
    return true;
}

bool cpu_loaded(size_t cpu_index) {
    return cpu_index < MAX_CPU_GDT_STATES &&
        __atomic_load_n(&g_loaded[cpu_index], __ATOMIC_ACQUIRE) != 0U;
}

size_t loaded_cpu_count() {
    return __atomic_load_n(&g_loaded_count, __ATOMIC_ACQUIRE);
}

uintptr_t kernel_entry_stack_top() {
    return kernel_entry_stack_top_for_cpu(0U);
}

uintptr_t kernel_entry_stack_top_for_cpu(size_t cpu_index) {
    if (cpu_index >= MAX_CPU_GDT_STATES) return 0U;
    return reinterpret_cast<uintptr_t>(
        g_kernel_entry_stacks[cpu_index] + KERNEL_ENTRY_STACK_SIZE);
}

bool set_kernel_stack(uintptr_t stack) {
    return set_kernel_stack_for_cpu(0U, stack);
}

bool set_kernel_stack_for_cpu(size_t cpu_index, uintptr_t stack) {
    if (!initialized() || cpu_index >= MAX_CPU_GDT_STATES ||
        !cpu_loaded(cpu_index) || stack == 0U || !is_canonical(stack) ||
        (stack & UINT64_C(0xF)) != 0U) {
        return false;
    }
    g_tss[cpu_index].rsp0 = stack;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    return true;
}

uintptr_t rsp0() {
    return rsp0_for_cpu(0U);
}

uintptr_t rsp0_for_cpu(size_t cpu_index) {
    if (cpu_index >= MAX_CPU_GDT_STATES || !cpu_loaded(cpu_index)) return 0U;
    return static_cast<uintptr_t>(g_tss[cpu_index].rsp0);
}

} // namespace arch::x86_64::gdt

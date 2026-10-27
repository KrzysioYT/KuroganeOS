#pragma once

#include <stddef.h>
#include <stdint.h>

namespace arch::x86_64::gdt {

constexpr uint16_t KERNEL_CODE_SELECTOR = 0x08;
constexpr uint16_t KERNEL_DATA_SELECTOR = 0x10;
constexpr uint16_t USER_DATA_SELECTOR = 0x1B;
constexpr uint16_t USER_CODE_SELECTOR = 0x23;
constexpr uint16_t TSS_SELECTOR = 0x28;

constexpr uint8_t DOUBLE_FAULT_IST = 1;
constexpr uint8_t NMI_IST = 2;
constexpr uint8_t MACHINE_CHECK_IST = 3;

constexpr size_t KERNEL_ENTRY_STACK_SIZE = 64 * 1024;
constexpr size_t MAX_CPU_GDT_STATES = 64;

// Initializes CPU0/BSP descriptor state. APs must call load_current_cpu() before
// entering any Ring-3-capable scheduler path.
void initialize();
bool initialized();
bool load_current_cpu(size_t cpu_index);
bool cpu_loaded(size_t cpu_index);
size_t loaded_cpu_count();

uintptr_t kernel_entry_stack_top();
uintptr_t kernel_entry_stack_top_for_cpu(size_t cpu_index);
bool set_kernel_stack(uintptr_t stack_top);
bool set_kernel_stack_for_cpu(size_t cpu_index, uintptr_t stack_top);
uintptr_t rsp0();
uintptr_t rsp0_for_cpu(size_t cpu_index);

} // namespace arch::x86_64::gdt

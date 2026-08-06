#pragma once

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

// Installs a kernel-owned long-mode GDT and reloads all segment registers.
// Interrupts must remain disabled until a matching IDT has been installed.
void initialize();
bool initialized();

} // namespace arch::x86_64::gdt

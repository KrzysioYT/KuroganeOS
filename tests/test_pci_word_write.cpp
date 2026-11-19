#include <cassert>
#include <cstdint>
#include <cstdio>

// Include the real I/O header first so only this translation unit substitutes
// port accesses; the PCI implementation below remains unchanged.
#include "../kernel/arch/x86_64/io.hpp"
namespace pci_test_io {
uint32_t address;
uint16_t command;
uint16_t status;
unsigned data_dword_writes;
unsigned data_word_writes;
constexpr uint64_t interrupt_flag = UINT64_C(1) << 9U;
bool interrupts_enabled;
bool interrupt_pending;

uint64_t read_flags() { return interrupts_enabled ? interrupt_flag : 0U; }
void disable_interrupts() { interrupts_enabled = false; }
void enable_interrupts() { interrupts_enabled = true; }

void out32(uint16_t port, uint32_t value) {
    if (port == 0xCF8U) {
        address = value;
        // An interrupt handler using the same selector would replace it.
        if (interrupts_enabled && interrupt_pending) address = 0x80000000U;
        return;
    }
    assert(port == 0xCFCU);
    ++data_dword_writes;
    command = static_cast<uint16_t>(value);
    status &= static_cast<uint16_t>(~(value >> 16U));
}
void out16(uint16_t port, uint16_t value) {
    ++data_word_writes;
    if (port == 0xCFCU) command = value;
    else { assert(port == 0xCFEU); status &= static_cast<uint16_t>(~value); }
}
uint32_t in32(uint16_t port) {
    assert(port == 0xCFCU);
    return command | (static_cast<uint32_t>(status) << 16U);
}
}
#define arch pci_test_io
#include "../kernel/drivers/pci.cpp"
#undef arch

int main() {
    using namespace pci_test_io;
    const pci::Address device{2U, 3U, 1U};
    constexpr uint32_t command_selector =
        0x80000000U | (2U << 16U) | (3U << 11U) | (1U << 8U) | 4U;
    command = 0U;
    status = 0xF900U;
    interrupts_enabled = true;
    interrupt_pending = true;
    pci::write16(device, 0x04U, 0x0406U);
    assert(command == 0x0406U && status == 0xF900U);
    assert(address == command_selector);
    assert(interrupts_enabled);

    interrupts_enabled = false;
    pci::write16(device, 0x06U, 0x8000U);
    assert(command == 0x0406U && status == 0x7900U);
    assert(data_dword_writes == 0U && data_word_writes == 2U);
    assert(!interrupts_enabled);

    interrupts_enabled = true;
    assert(pci::read32(device, 0x04U) == 0x79000406U);
    assert(address == command_selector && interrupts_enabled);
    pci::write32(device, 0x04U, 0x08000002U);
    assert(address == command_selector && interrupts_enabled);
    assert(command == 0x0002U && status == 0x7100U);
    assert(data_dword_writes == 1U && data_word_writes == 2U);
    std::puts("PCI config accesses preserve selector, interrupt state and adjacent W1C status: PASS");
}

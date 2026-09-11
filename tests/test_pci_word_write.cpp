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
void out32(uint16_t port, uint32_t value) {
    if (port == 0xCF8U) { address = value; return; }
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
    command = 0U;
    status = 0xF900U;
    pci::write16(device, 0x04U, 0x0406U);
    assert(command == 0x0406U && status == 0xF900U);
    assert(address == (0x80000000U | (2U << 16U) | (3U << 11U) | (1U << 8U) | 4U));
    pci::write16(device, 0x06U, 0x8000U);
    assert(command == 0x0406U && status == 0x7900U);
    assert(data_dword_writes == 0U && data_word_writes == 2U);
    std::puts("PCI word writes preserve adjacent W1C status: PASS");
}

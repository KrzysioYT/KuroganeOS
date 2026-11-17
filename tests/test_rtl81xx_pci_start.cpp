#include "../kernel/net/rtl81xx.cpp"
#include <cassert>

namespace {
uint16_t command;
uint16_t writable;
bool disappear_after_write;
unsigned writes;
}

namespace pci {
uint16_t read16(Address, uint8_t offset) {
    assert(offset == 0x04U);
    return command;
}

void write16(Address, uint8_t offset, uint16_t value) {
    assert(offset == 0x04U);
    ++writes;
    command = disappear_after_write ? UINT16_MAX :
        static_cast<uint16_t>((command & ~writable) | (value & writable));
}
}

int main() {
    using namespace net::rtl81xx;
    const pci::Device device{};
    writable = UINT16_MAX;
    command = 0x0105U;  // Firmware left DMA and I/O enabled, memory disabled.
    assert(configure_pci_command(device, false));
    assert(command == 0x0103U);
    assert(configure_pci_command(device, true));
    assert(command == 0x0107U);

    writable = 0x0002U; // Bus mastering cannot be stopped.
    assert(!configure_pci_command(device, false));
    command = 0x0000U;
    writable = 0x0004U; // Memory decoding cannot be enabled.
    assert(!configure_pci_command(device, false));
    command = 0x0002U;
    writable = 0U;      // Device does not accept the DMA enable command.
    assert(!configure_pci_command(device, true));

    writable = UINT16_MAX;
    disappear_after_write = true;
    assert(!configure_pci_command(device, true));
    const unsigned writes_before = writes;
    assert(!configure_pci_command(device, false));
    assert(writes == writes_before);

    uint8_t mmio[MMIO_BYTES]{};
    g_device.registers = mmio;
    g_device.started = true;
    assert(!link_up());
    mmio[REG_PHY_STATUS] = PHY_LINK_UP;
    assert(!link_up()); // Carrier cannot make a stopped DMA engine ready.
    mmio[REG_CHIP_CMD] = CMD_RX | CMD_TX;
    assert(link_up());
    mmio[REG_PHY_STATUS] = UINT8_MAX;
    assert(!link_up());
}

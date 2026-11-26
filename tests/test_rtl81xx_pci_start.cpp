#include "../kernel/net/rtl81xx.cpp"
#include <cassert>

namespace {
constexpr uint32_t realtek_identity = UINT32_C(0x816810EC);
constexpr uint32_t foreign_identity = UINT32_C(0x100E8086);
uint32_t pci_identity = realtek_identity;
uint32_t replace_after_write;
uint16_t command;
uint16_t writable;
bool disappear_after_write;
unsigned command_reads;
unsigned writes;
}

namespace pci {
uint64_t bar_address(const Device&, uint8_t index, bool* io) {
    assert(index == 2U || index == 0U);
    *io = false;
    return 0x8000U;
}

uint32_t read32(Address, uint8_t offset) {
    assert(offset == 0U);
    return pci_identity;
}
uint16_t read16(Address, uint8_t offset) {
    assert(offset == 0x04U);
    ++command_reads;
    return command;
}

void write16(Address, uint8_t offset, uint16_t value) {
    assert(offset == 0x04U);
    ++writes;
    command = disappear_after_write ? UINT16_MAX :
        static_cast<uint16_t>((command & ~writable) | (value & writable));
    if (replace_after_write != 0U) pci_identity = replace_after_write;
}
}

int main() {
    using namespace net::rtl81xx;
    pci::Device device{};
    device.vendor_id = 0x10ECU;
    device.device_id = 0x8168U;
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

    disappear_after_write = false;
    command = 0x0002U;
    const unsigned reads_before = command_reads;
    pci_identity = UINT32_MAX;
    assert(!configure_pci_command(device, true));
    pci_identity = foreign_identity;
    assert(!configure_pci_command(device, false));
    assert(command_reads == reads_before && writes == writes_before);

    // Matching command bits after the write do not verify the NIC if
    // another function is now present at the cached PCI address.
    pci_identity = realtek_identity;
    replace_after_write = foreign_identity;
    assert(!configure_pci_command(device, true));
    assert(command == 0x0006U);
    const unsigned writes_after_replacement = writes;
    assert(!configure_pci_command(device, false));
    assert(writes == writes_after_replacement);

    uint8_t mmio[MMIO_BYTES]{};
    pci_identity = realtek_identity;
    replace_after_write = 0U;
    g_device.pci_device = device;
    command = 6U;
    g_device.bar_physical = 0x8000U;
    g_device.bar_index = 2U;
    g_device.registers = mmio;
    g_device.started = true;
    assert(!link_up());
    mmio[REG_PHY_STATUS] = PHY_LINK_UP;
    assert(!link_up()); // Carrier cannot make a stopped DMA engine ready.
    mmio[REG_CHIP_CMD] = CMD_RX | CMD_TX;
    assert(link_up());
    g_device.bar_index = 0U;
    assert(link_up()); // Selected BAR0 fallback is also validated live.
    command = 2U;
    assert(registers_available(false));
    assert(!registers_available(true));
    assert(!link_up());
    command = 4U;
    assert(!link_up());
    command = UINT16_MAX;
    assert(!link_up());
    command = 6U;
    assert(link_up());
    mmio[REG_PHY_STATUS] = UINT8_MAX;
    assert(!link_up());
}

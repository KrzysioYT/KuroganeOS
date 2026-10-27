#!/usr/bin/env python3
"""CI-only pre-bind E1000e PM exercise; never suspend an active driver."""
from pathlib import Path


def inject(root=Path('.')):
    driver = root / 'kernel/net/e1000.cpp'
    source = driver.read_text()
    anchor = '    g_device = {};\n    g_device.pci_device = *pci_device;'
    if source.count(anchor) != 1 or 'qualify_pci_power_runtime' in source:
        raise ValueError('PCI power qualification anchor changed/already injected')
    source = '''#include "../drivers/pci_power.hpp"
#include "../arch/x86_64/hpet.hpp"
#include "../terminal.hpp"
''' + source
    helper = r'''
namespace {
uint16_t pm_read16(pci::Address address, uint8_t offset, void*) {
    return pci::read16(address, offset);
}
void pm_write16(pci::Address address, uint8_t offset, uint16_t value, void*) {
    pci::write16(address, offset, value);
}
bool pm_wait_us(uint32_t microseconds, void*) {
    namespace timer = arch::x86_64::hpet;
    if (!timer::initialized() || timer::counter_period_femtoseconds() == 0U) {
        return false;
    }
    const uint64_t period = timer::counter_period_femtoseconds();
    const uint64_t ticks =
        (static_cast<uint64_t>(microseconds) * UINT64_C(1000000000) + period - 1U) / period;
    const uint64_t start = timer::counter();
    for (uint32_t budget = 0U; budget < 20000000U; ++budget) {
        if (timer::counter() - start >= ticks) return true;
        __asm__ volatile("pause" : : : "memory");
    }
    return false;
}

bool qualify_pci_power_runtime(const pci::Device& device) {
    using namespace pci::power;
    using PowerStatus = pci::power::Status;
    // The qualification selects a single emulated 82574L before the driver
    // owns MMIO, DMA or interrupts. This is not a runtime suspend callback.
    if (device.vendor_id != 0x8086U || device.device_id != 0x10D3U ||
        (device.header_type & 0x7FU) != 0U) return false;
    pci::Capability cap{};
    if (!pci::find_capability(device, pci::CapabilityId::PowerManagement, &cap)) {
        return false;
    }
    const ConfigAccess access{pm_read16, pm_write16, nullptr, pm_wait_us};
    CapabilityInfo info{};
    if (inspect_capability(device.address, cap.offset, access, &info) != PowerStatus::Ok ||
        info.current_state != State::D0 || !pm_wait_us(1U, nullptr)) return false;
    const uint16_t command = pci::read16(device, 0x04U);
    uint32_t bars[6]{};
    for (uint8_t i = 0U; i < 6U; ++i) bars[i] = pci::read32(device, 0x10U + i * 4U);
    const uint32_t rom = pci::read32(device, 0x30U);
    const uint16_t cache_latency = pci::read16(device, 0x0CU);
    const uint16_t irq = pci::read16(device, 0x3CU);
    pci::write16(device, 0x04U, command & ~UINT16_C(7));
    for (unsigned cycle = 0U; cycle < 4U; ++cycle) {
        Transaction transaction{};
        const PowerStatus down = transition(device.address, cap.offset,
            State::D3Hot, access, &transaction);
        if (down != PowerStatus::Ok) {
            terminal::println(pci::power::status_name(down));
            return false;
        }
        if ((pci::read16(device, cap.offset + 4U) & 3U) != 3U) return false;
        const PowerStatus up = restore(access, &transaction);
        if (up != PowerStatus::Ok || transaction.active ||
            (pci::read16(device, cap.offset + 4U) & 3U) != 0U) {
            terminal::println(pci::power::status_name(up));
            return false;
        }
        // D3hot->D0 may reset endpoint configuration. Restore only writable
        // Type-0 fields, with decoding/DMA off; never replay PCI status W1C.
        pci::write16(device, 0x04U, command & ~UINT16_C(7));
        for (uint8_t i = 0U; i < 6U; ++i) {
            pci::write32(device, 0x10U + i * 4U, bars[i]);
            if (pci::read32(device, 0x10U + i * 4U) != bars[i]) return false;
        }
        pci::write32(device, 0x30U, rom);
        pci::write16(device, 0x0CU, cache_latency);
        pci::write16(device, 0x3CU, irq);
        terminal::println("[PCI-PM-TEST] D0 -> D3hot -> D0 cycle: PASS");
    }
    // Firmware DMA ownership is not resumed: the driver will enable Bus Master
    // after mapping and then initialize its own hardware queues.
    pci::write16(device, 0x04U, command & ~PCI_COMMAND_BUS_MASTER);
    terminal::println("[TEST] pci_power_cycle: PASS");
    return true;
}
} // namespace

'''
    entry = 'Status initialize() {'
    if source.count(entry) != 1:
        raise ValueError('E1000 initialize anchor changed')
    source = source.replace(entry, helper + entry)
    source = source.replace(anchor, '''    if (!qualify_pci_power_runtime(*pci_device)) {
        terminal::println("[TEST] pci_power_cycle: FAIL");
        g_status = Status::DeviceError;
        return g_status;
    }
''' + anchor)
    driver.write_text(source)


if __name__ == '__main__':
    inject()

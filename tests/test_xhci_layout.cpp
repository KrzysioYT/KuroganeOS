#include <cassert>
#include <cstdint>
#include <iostream>

#include "../kernel/drivers/usb/xhci_layout.hpp"

int main() {
    constexpr size_t mmio = 64U * 1024U;
    using drivers::usb::xhci::LayoutStatus;
    using drivers::usb::xhci::validate_capability_layout;

    assert(validate_capability_layout(
        0x20U, 0x1000U, 0x2000U, 32U, 4U, mmio) ==
        LayoutStatus::Ok);
    assert(validate_capability_layout(
        0x1FU, 0x1000U, 0x2000U, 32U, 4U, mmio) ==
        LayoutStatus::InvalidCapabilityLength);
    assert(validate_capability_layout(
        0x20U, 0x1000U, 0x2000U, 0U, 4U, mmio) ==
        LayoutStatus::NoSlots);
    assert(validate_capability_layout(
        0x20U, 0x1000U, 0x2000U, 32U, 0U, mmio) ==
        LayoutStatus::NoPorts);
    assert(validate_capability_layout(
        0x20U, 0xFFFFU, 0x2000U, 32U, 4U, mmio) ==
        LayoutStatus::DoorbellWindowOutOfRange);
    assert(validate_capability_layout(
        0x20U, 0x1000U, 0xFFFFFFE0U, 32U, 4U, mmio) ==
        LayoutStatus::RuntimeWindowOutOfRange);
    assert(validate_capability_layout(
        0x20U, 0x0800U, 0x1000U, 32U, 255U, 0x13E0U) ==
        LayoutStatus::PortWindowOutOfRange);
    assert(validate_capability_layout(
        0x20U, 0x1000U, 0x2000U, 255U, 4U, mmio) ==
        LayoutStatus::Ok);
    assert(validate_capability_layout(
        0x20U, 0x1000U, 0x2000U, 32U, 4U, 0x3FU) ==
        LayoutStatus::InvalidMmioWindow);
    assert(drivers::usb::xhci::layout_status_message(
        LayoutStatus::RuntimeWindowOutOfRange) != nullptr);
    std::cout << "xHCI layout validation: PASS\n";
    return 0;
}

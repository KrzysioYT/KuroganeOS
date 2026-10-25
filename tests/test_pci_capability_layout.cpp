#include <assert.h>
#include <stdint.h>

#include "../kernel/drivers/pci.hpp"

int main() {
    // 32-bit MSI without masking occupies ten bytes.
    assert(pci::msi_capability_layout_valid(0xF4U, 0U));
    assert(!pci::msi_capability_layout_valid(0xF8U, 0U));

    // 64-bit MSI needs fourteen bytes. Per-vector masking additionally
    // requires complete mask and pending-bit dwords.
    assert(pci::msi_capability_layout_valid(
        0xF0U, UINT16_C(1) << 7U));
    assert(!pci::msi_capability_layout_valid(
        0xF4U, UINT16_C(1) << 7U));
    assert(pci::msi_capability_layout_valid(
        0xE8U, (UINT16_C(1) << 7U) | (UINT16_C(1) << 8U)));
    assert(!pci::msi_capability_layout_valid(
        0xECU, (UINT16_C(1) << 7U) | (UINT16_C(1) << 8U)));

    // MSI-X is a fixed twelve-byte capability. A descriptor at 0xf8 would
    // previously wrap the PBA read to PCI config offset 0x00.
    assert(pci::msix_capability_layout_valid(0xF4U));
    assert(!pci::msix_capability_layout_valid(0xF8U));
    assert(!pci::msix_capability_layout_valid(0x41U));
    return 0;
}

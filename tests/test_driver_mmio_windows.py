#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")

def hex_constant(source: str, name: str) -> int:
    pattern = rf"constexpr\s+(?:u?int\w*_t|uintptr_t|uint64_t)\s+{re.escape(name)}\s*=\s*UINT64_C\((0x[0-9A-Fa-f]+)\)"
    match = re.search(pattern, source)
    if not match:
        raise AssertionError(f"missing {name}")
    return int(match.group(1), 16)

xhci = read("kernel/drivers/usb/xhci.cpp")
virtio = read("kernel/net/virtio_net.cpp")
apic = read("kernel/arch/x86_64/apic.cpp")

xhci_base = hex_constant(xhci, "MMIO_VIRTUAL_BASE")
virtio_common = hex_constant(virtio, "kCommonVirtualBase")
virtio_notify = hex_constant(virtio, "kNotifyVirtualBase")
virtio_device = hex_constant(virtio, "kDeviceVirtualBase")
virtio_msix = hex_constant(virtio, "kMsixTableVirtualBase")
virtio_pba = hex_constant(virtio, "kMsixPendingVirtualBase")
apic_local = hex_constant(apic, "LOCAL_VIRTUAL_BASE")
apic_io = hex_constant(apic, "IO_VIRTUAL_BASE")

xhci_end = xhci_base + 64 * 1024

assert not (xhci_base <= virtio_common < xhci_end), "VirtIO common config overlaps xHCI MMIO"
assert not (xhci_base <= virtio_notify < xhci_end), "VirtIO notify config overlaps xHCI MMIO"
assert not (xhci_base <= virtio_device < xhci_end), "VirtIO device config overlaps xHCI MMIO"
assert len({virtio_common, virtio_notify, virtio_device, virtio_msix, virtio_pba}) == 5
assert virtio_common != apic_local, "VirtIO common config overlaps Local APIC window"
assert virtio_msix != apic_io, "VirtIO MSI-X table overlaps I/O APIC window"
assert all((value >> 40) != (apic_local >> 40) for value in (
    virtio_common, virtio_notify, virtio_device, virtio_msix, virtio_pba
)), "VirtIO window shares the reserved APIC B3 region"

print("Driver MMIO coexistence layout: PASS")

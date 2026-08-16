# Hardware support

“Supported” means exercised on the QEMU q35/EDK2 reference machine. Discovery
without active routing is identified separately. Physical hardware has not
received broad qualification.

| Device/technology | Status | Scope |
|---|---|---|
| x86-64 | Supported | one CPU, long mode, 4-level paging |
| UEFI | Supported | EDK2, memory map, ACPI pointer, ExitBootServices |
| GOP framebuffer | Supported | linear software-rendered framebuffer |
| PS/2 keyboard | Supported | IRQ/poll input path |
| PS/2 mouse | Supported | 3/4-byte packets, wheel, buttons |
| PCI | Supported | bounded legacy config-space enumeration |
| ACPI RSDP/RSDT/XSDT/MADT | Supported discovery | checksummed, bounded topology parsing |
| Local APIC / I/O APIC | Discovery only | MMIO identity/topology; PIC remains active route |
| 8259 PIC + PIT | Supported | active IRQ routing and preemption timer |
| AHCI SATA | Supported | QEMU controller, DMA read/write/flush, 512-byte sectors |
| GPT | Supported | primary validation and installer primary/backup creation |
| Intel 82540EM/E1000 | Supported | PCI `8086:100e`, QEMU user networking |
| xHCI | Partial | polled controller, one USB HID boot-keyboard path |
| USB HID keyboard | Supported on xHCI path | boot protocol, QEMU `usb-kbd` |
| USB mouse/gamepad/storage | Not supported | no class drivers |
| NVMe | Not supported | no controller/namespace driver |
| IDE/PATA | Not supported | optical ISO is firmware boot media only |
| VirtIO block/network | Not supported | feature flags remain disabled |
| Wi-Fi/Bluetooth | Not supported | no bus/protocol drivers |
| Audio | Not supported | no audio stack |
| GPU acceleration | Not supported | GOP framebuffer only |
| SMP | Not supported | APs discovered but not started/scheduled |
| BIOS/CSM boot | Not supported | UEFI x86-64 only |

Drivers use supervisor-only MMIO/DMA mappings and bounded descriptors. Userspace
has no direct I/O-port, MMIO, DMA or PCI access. See
[limitations.md](limitations.md) before attempting physical hardware.

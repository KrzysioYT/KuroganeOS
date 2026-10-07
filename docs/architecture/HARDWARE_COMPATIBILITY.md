# KuroganeOS Hardware Compatibility Architecture

## Purpose

KuroganeOS targets generic **x86-64 UEFI PCs**. QEMU, Oracle VirtualBox and
VMware are test environments, not architectural platforms. Code above a device
backend must not need to know which hypervisor or physical machine is running
the system.

The compatibility rule is:

```text
firmware / physical or virtual hardware
        |
        v
bus discovery -> Device Model -> Driver Manager -> subsystem interface
                                              |
                                              v
                           input / block / network / audio / display
                                              |
                                              v
                              kernel services / Flux / applications
```

Hypervisor-specific checks are allowed only inside a backend that implements a
real device contract, for example VirtIO. Windowing, VFS, networking protocols,
applications and generic input routing must not contain `if VirtualBox`,
`if QEMU` or equivalent platform shortcuts.

## Boot policy

Hardware availability is divided into boot-critical infrastructure and optional
capabilities.

Boot-critical in the first 5.1 portability slice:

- monotonic timer needed by the current scheduler;
- timer interrupt scheduling hook;
- generic input event queue.

Optional hardware backends:

- PS/2 keyboard;
- PS/2 pointer;
- xHCI/USB;
- additional storage controllers and media;
- physical networking;
- audio;
- additional CPUs.

An optional backend may report `DEGRADED`, `SKIP`, `NotSupported` or
`NoDevice`, but absence of that backend must not halt an otherwise bootable
system. A device driver failure must stay inside its device/subsystem unless
continuing would corrupt memory, DMA ownership or persistent storage.

The Device Model records this contract explicitly as
`Requirement::Optional` or `Requirement::BootCritical`. Driver Manager
aggregate binding propagates failures only for devices marked boot-critical;
an optional device may enter `FAILED` while the rest of the system continues.

## Input contract

All pointer and keyboard transports converge on the generic input queue.

```text
PS/2 keyboard -----+
USB HID keyboard --+--> input::Event --> Window Manager / console / apps
future HID/I2C -----+

PS/2 mouse --------+
USB HID mouse -----+--> input::Event --> Window Manager / apps
USB tablet --------+
future touchpad ----+
```

PS/2 is a compatibility backend, not the definition of keyboard or pointer
availability. 5.1 begins by removing PS/2 from the boot-critical set. 5.2 must
extend HID beyond the current boot-protocol devices so USB tablets, report
protocol mice and other common HID transports can coexist without platform
special cases.

## Storage contract

Storage consumers use the block-device API rather than a controller-specific
path.

```text
NVMe -------+
AHCI/SATA --+--> storage::block::Device --> VFS / filesystems / installer
USB MSC ----+
```

The installer must select from discovered block devices and must not require a
specific hypervisor controller.

## Display contract

UEFI GOP framebuffer remains the baseline display path through the compatibility
phase. Hardware GPU acceleration is an additional backend, never a prerequisite
for reaching a usable desktop.

## Network and audio

Networking and audio are optional at boot. Supported adapters/codecs may come
online after discovery; unsupported hardware leaves the subsystem unavailable
without taking down the kernel. DHCP or gateway failure may degrade networking,
but must not halt the kernel. Qualification jobs for a specific NIC or audio
backend still fail when that backend is explicitly under test.

## Compatibility tiers

- **Tier 0 — Boot:** x86-64 UEFI, CPU, memory, timer and framebuffer.
- **Tier 1 — Usable:** keyboard/pointer and persistent block storage.
- **Tier 2 — Connected:** networking, audio and general USB.
- **Tier 3 — Extended:** accelerated GPU, Wi-Fi, Bluetooth, touchpad/I2C and
  vendor-specific laptop hardware.

A machine may boot at a lower tier when an optional driver is unavailable.

## Required test matrix

Portability work is not qualified by one emulator.

1. QEMU TCG/KVM with legacy devices.
2. QEMU with i8042 disabled and USB-only keyboard/mouse.
3. Oracle VirtualBox with the documented reference profile.
4. VMware once a repeatable automated/manual profile exists.
5. Physical Intel and AMD systems as hardware becomes available.

The automated matrix must distinguish a missing optional capability from a
failure of a capability explicitly being qualified.

## Current 5.1 starting point

Already available before this refactor: Device Model 2.0, Driver Manager 2.0,
PCI discovery, MSI/MSI-X, ACPI/APIC/HPET, AHCI, NVMe, xHCI, USB HID boot
keyboard/mouse, USB Mass Storage, HDA/AC'97, a unified input queue and a unified
block-device registry.

The first portability slice adds a central hardware boot policy, explicit
per-device optional/boot-critical requirements and removes PS/2 keyboard/mouse
from the kernel's boot-critical condition. A dedicated runtime gate boots QEMU
with `q35,i8042=off` and requires real xHCI USB keyboard and mouse delivery.

Known follow-up work includes HID report-protocol/absolute pointer support,
broader PCI NIC coverage, laptop touchpads/I2C HID, Wi-Fi/Bluetooth and wider
physical-machine qualification.

# KuroganeOS Hardware Compatibility Matrix

This matrix describes the current 5.x hardware-compatibility line and
distinguishes automated evidence from external validation. An unavailable
external environment is never interpreted as PASS.

| Platform/profile | Evidence type | Current 5.x status |
| --- | --- | --- |
| QEMU q35 / OVMF / legacy-compatible devices | Automated | Gate defined |
| QEMU q35 / i8042 off / xHCI USB keyboard + boot mouse | Automated | Gate defined |
| QEMU q35 / i8042 off / xHCI USB keyboard + report-protocol USB tablet | Automated | Gate defined |
| QEMU q35 / NVMe + xHCI USB Mass Storage | Automated | Gate defined |
| QEMU q35 / GenuineIntel CPUID / 4 vCPU | Automated | Gate defined |
| QEMU q35 / AuthenticAMD CPUID / 4 vCPU | Automated | Gate defined |
| QEMU Tier 1 Usable profile | Automated | Gate defined |
| QEMU Tier 3 Extended profile | Automated | Gate defined |
| Oracle VirtualBox on a real host | External | UNVERIFIED for current candidate |
| VMware on a real host | External | UNVERIFIED |
| Physical Intel desktop/laptop | External | UNVERIFIED |
| Physical AMD desktop/laptop | External | UNVERIFIED |

## Capability interpretation

A platform is reported from runtime capabilities rather than from its brand or
hypervisor identity.

- Tier 0 Boot: timer/scheduling/input infrastructure + display.
- Tier 1 Usable: Tier 0 + keyboard + pointer + block storage.
- Tier 2 Connected: Tier 1 + network + audio.
- Tier 3 Extended: Tier 2 + USB host + multiprocessor runtime.

The tier is not a promise that every peripheral in the machine is supported.
For example, a laptop may be Tier 1 through USB input and NVMe while its Wi-Fi
or I2C touchpad is still unsupported.

## Current supported backend foundation

Input:
- PS/2 keyboard/mouse compatibility path;
- xHCI USB boot keyboard;
- xHCI USB boot mouse;
- xHCI HID report-protocol relative/absolute pointer foundation.

Storage:
- AHCI/SATA;
- NVMe;
- xHCI USB Mass Storage.

Network:
- E1000;
- PCnet legacy coverage;
- VirtIO-net.

Audio:
- Intel HDA;
- AC'97 compatibility.

Display:
- UEFI GOP framebuffer/software rendering baseline.

Boot/platform:
- x86-64 UEFI;
- ACPI MADT/FADT/DSDT discovery;
- APIC/IOAPIC with legacy interrupt fallbacks;
- HPET with PIT fallback where applicable;
- multi-CPU AP startup/cross-CPU work/TLB shootdown foundation.

## Known non-claims

5.x does not claim universal PC support. In particular, the current foundation
does not imply support for arbitrary modern Wi-Fi, Bluetooth, HID-over-I2C
touchpads, vendor-specific laptop controllers, or accelerated Intel/AMD/NVIDIA
graphics.

Those devices belong to later Driver & Device Expansion work. Unsupported
optional hardware must remain diagnosable and must not prevent an otherwise
valid lower-tier boot.

For the VirtualBox reference setup see [../VIRTUALBOX.md](../VIRTUALBOX.md).

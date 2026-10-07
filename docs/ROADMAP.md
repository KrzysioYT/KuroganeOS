# KuroganeOS active roadmap

Qualified baseline: **5.0.0-dev — Steel / Hardware — QUALIFIED AFTER AUDIT**.  
Active engineering track: **5.1 — HAL / Driver Portability**.

The authoritative Road-to-15 plan is
[`docs/roadmap/MASTER_ROADMAP_15.md`](roadmap/MASTER_ROADMAP_15.md), and the
current evidence summary is
[`docs/ROAD_TO_15_STATUS.md`](ROAD_TO_15_STATUS.md).

The large subsystem checklist that previously lived in this file described the
old 3.3-era state and had become materially stale. Git history remains the
source for that historical checklist; milestone qualification now follows the
Road-to-15 evidence model.

## 5.0 Steel — qualified after coexistence audit

The implemented 5.0 scope contains:

- validated PCI BAR/capability handling, MSI/MSI-X and I/O APIC routing;
- ACPI MADT, HPET, FADT/DSDT reset and S5 power discovery;
- AHCI, writable NVMe and writable xHCI USB Mass Storage;
- xHCI HID keyboard/mouse;
- Intel HDA PCM DMA plus AC'97 compatibility;
- unified storage/NIC selection;
- AP startup, per-CPU AP stacks/state, Local APIC IPIs, cross-CPU work and
  synchronous TLB shootdown.

The `smp_cross_cpu_work` marker is intentionally not named
`smp_scheduler`. Full Scheduler 2.0 and multicore userspace scheduling are
6.0 Core Steel tasks.

Authoritative post-audit runtime candidate:
`569bae4c33aa0c04db87f0cbe785da1aa45cc084`.

The external QEMU audit correctly found two matrix holes in the earlier
candidate: xHCI/VirtIO shared a fixed virtual MMIO region and xHCI exposed only
one active HID slot. The post-audit candidate closes both:

1. VirtIO-net uses dedicated `0xFFFFB7...` virtual space and is protected by
   a host overlap regression against xHCI and APIC;
2. xHCI supports an independent companion HID context so USB keyboard and USB
   mouse can enumerate, queue transfers, deliver input and hotplug independently;
3. the final Steel Closeout now directly requires keyboard + mouse + VirtIO
   networking together, not merely isolated driver gates.

Authoritative post-audit runs:
- Steel Closeout `37661212373` — PASS;
- xHCI/VirtIO coexistence `37661212429` — PASS;
- xHCI Multi HID `37661212451` — PASS;
- USB Keyboard `37661212449` — PASS;
- USB Mouse `37661212378` — PASS;
- USB Mass Storage `37661212361` — PASS;
- NVMe `37661212483` — PASS;
- Intel HDA `37661212532` — PASS;
- HPET `37661212464` — PASS;
- Steel Foundations `37661212233` — PASS.

USB Mass Storage, NVMe, SMP and Intel HDA were absent in the older audited
`2d550f1` source but are implemented and requalified on the current line.
Mouse wheel/report-protocol support remains an explicit P2 limitation and is
not claimed as part of the Boot Mouse qualification.

## Active 5.x compatibility bridge

5.0 remains the qualified Steel baseline. Before Core 2.0, the 5.x line now
turns that hardware substrate into a generic x86-64 UEFI platform:

- 5.1 — HAL / Driver Portability — **IN PROGRESS**
- 5.2 — Universal Input — pending
- 5.3 — Universal Storage — pending
- 5.4 — Platform / Firmware Portability — pending
- 5.5 — Hardware Compatibility Gate — pending

The architectural contract is documented in
[`docs/architecture/HARDWARE_COMPATIBILITY.md`](architecture/HARDWARE_COMPATIBILITY.md).

## Road to 15

- 5.0.0-dev — Steel / Hardware — **QUALIFIED AFTER AUDIT**
- 5.1–5.5 — Hardware Compatibility Foundation — active/pending
- 6.0.0-dev — Kernel Core 2.0 — pending
- 7.0.0-dev — Driver & Device Expansion — pending
- 8.0.0-dev — Flux Desktop Platform — pending
- 9.0.0-dev — Networking & Services — pending
- 10.0.0-dev — Storage & VFS 2.0 — pending
- 11.0.0-dev — Application Platform — pending
- 12.0.0-dev — Security, Updates & Recovery — pending
- 13.0.0-dev — Performance & Power — pending
- 14.0.0-rc — Compatibility / Release Qualification — pending
- 15.0.0 — STABLE — final target

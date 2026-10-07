# KuroganeOS active roadmap

Current baseline: **5.0.0-dev — Steel / Hardware — QUALIFIED AFTER AUDIT**.

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

## Road to 15

- 5.0.0-dev — Steel / Hardware — **QUALIFIED AFTER AUDIT**
- 6.0.0-dev — Core Steel — pending
- 7.0.0-dev — Iron Shield — pending
- 8.0.0-dev — Connected Steel — pending
- 9.0.0-dev — Forge Graphics — pending
- 10.0.0-dev — Steel Applications — pending
- 11.0.0-dev — Anvil — pending
- 12.0.0-dev — Platform / Web — pending
- 13.0.0-dev — Forge Design — pending
- 14.0.0-rc — Forge Desktop — pending
- 15.0.0 — STABLE — final target

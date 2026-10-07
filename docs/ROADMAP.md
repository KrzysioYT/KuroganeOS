# KuroganeOS active roadmap

Current baseline: **5.0.0-dev — Steel / Hardware — REOPENED**.

The authoritative Road-to-15 plan is
[`docs/roadmap/MASTER_ROADMAP_15.md`](roadmap/MASTER_ROADMAP_15.md), and the
current evidence summary is
[`docs/ROAD_TO_15_STATUS.md`](ROAD_TO_15_STATUS.md).

The large subsystem checklist that previously lived in this file described the
old 3.3-era state and had become materially stale. Git history remains the
source for that historical checklist; milestone qualification now follows the
Road-to-15 evidence model.

## 5.0 Steel — reopened after coexistence audit

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

Authoritative automated candidate:
`90d5ffc80f91236e10118324c8e3b5d6f4a1c781`.

The previous matrix passed on 2026-10-07, but a later runtime audit exposed two
missing coexistence cases that were not represented by those gates: xHCI with
VirtIO-net and simultaneous USB keyboard + mouse. 5.0 is therefore reopened.

Current repair order:
1. isolate xHCI and VirtIO-net kernel MMIO windows and qualify both MSI-X and
   polling fallback with real USB traffic plus DHCP/gateway;
2. replace the single active xHCI slot/device state with independent per-device
   contexts and qualify keyboard + mouse at the same time;
3. keep HID wheel/report-protocol support as a tracked P2 gap unless it becomes
   required by the formal Steel acceptance matrix.

USB Mass Storage, NVMe, SMP and Intel HDA are present on the current integration
line; the audit that reported them absent was run against the older `2d550f1`
baseline.

## Road to 15

- 5.0.0-dev — Steel / Hardware — **REOPENED**
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

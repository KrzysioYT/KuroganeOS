# KuroganeOS active roadmap

Current baseline: **5.0.0-dev — Steel / Hardware — QUALIFIED**.

The authoritative Road-to-15 plan is
[`docs/roadmap/MASTER_ROADMAP_15.md`](roadmap/MASTER_ROADMAP_15.md), and the
current evidence summary is
[`docs/ROAD_TO_15_STATUS.md`](ROAD_TO_15_STATUS.md).

The large subsystem checklist that previously lived in this file described the
old 3.3-era state and had become materially stale. Git history remains the
source for that historical checklist; milestone qualification now follows the
Road-to-15 evidence model.

## 5.0 Steel — qualified automated milestone

The qualified 5.0 scope contains:

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

All required 5.0 gates passed on 2026-10-07, including Steel Closeout,
HPET, SMP, HDA, NVMe, USB Mass Storage, USB Keyboard and USB Mouse. PR #45
then merged the candidate into the integration branch as
`8374b6ba66a8bb9a4d4ddf8f969816dc789a5be4`.

Oracle VirtualBox and physical-hardware testing remain external validation.
A real failure there reopens the affected 5.0 subsystem; lack of external
hardware evidence is never converted into a fake PASS.

## Current checkpoint

The project is intentionally paused at a clean 5.0 validation checkpoint for
user-side testing before dependent Core Steel work is treated as authoritative.
Use `docs/testing/STEEL_5_0_MANUAL.md` for the exact manual test sequence.

## Road to 15

- 5.0.0-dev — Steel / Hardware — **QUALIFIED**
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

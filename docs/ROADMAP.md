# KuroganeOS active roadmap

Current baseline: **5.0.0-dev — Steel / Hardware qualification candidate**.

The authoritative Road-to-15 plan is
[`docs/roadmap/MASTER_ROADMAP_15.md`](roadmap/MASTER_ROADMAP_15.md), and the
current evidence summary is
[`docs/ROAD_TO_15_STATUS.md`](ROAD_TO_15_STATUS.md).

The large subsystem checklist that previously lived in this file described the
old 3.3-era state and had become materially stale: it still reported SMP,
NVMe, Intel HDA and other already-developed Steel work as absent. Keeping that
snapshot as the "canonical" roadmap would make release qualification less
truthful, so 5.0 closeout retires it in favor of the milestone roadmap above.
Git history remains the source for the former 3.x checklist.

## Active gate — 5.0 Steel

The code-complete candidate contains:

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

5.0 moves from **QUALIFICATION CANDIDATE** to **QUALIFIED** only after the
exact candidate SHA passes Steel Closeout and the HPET/SMP/HDA/NVMe/USB
hardware matrix. User-side VirtualBox and physical-hardware tests follow that
automated candidate and are recorded as external evidence.

## Road to 15

- 5.0.0-dev — Steel / Hardware — qualification candidate
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

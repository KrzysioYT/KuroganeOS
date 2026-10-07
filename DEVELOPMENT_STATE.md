# KuroganeOS Development State

Last updated: 2026-10-07

## Integration branch

`gpt/road-to-15-consolidation`

5.0 Steel closeout candidate:

`90d5ffc80f91236e10118324c8e3b5d6f4a1c781`

Merged through PR #45 as integration commit:

`8374b6ba66a8bb9a4d4ddf8f969816dc789a5be4`

## Formal release track

- `3.3.3-dev — Red Flux`: QUALIFIED
- `3.4.0-dev — System Services`: QUALIFIED
- `3.5.0-dev — Connected Userspace`: QUALIFIED
- `3.6.0-dev — Flux Stabilization`: QUALIFIED
- `4.0.0-dev — Pre-Steel`: QUALIFIED
- `5.0.0-dev — Steel / Hardware`: REOPENED — coexistence audit
- `6.0.0-dev` through `14.0.0-rc`: pending
- `15.0.0 stable`: target

`5.0.0-dev` is the formal Steel milestone identity. The `-dev` suffix does
not mean the 5.0 engineering gate is incomplete; only 15.0.0 is planned as the
first product-level STABLE release.

## Steel implemented scope

- PCI/MSI/MSI-X and bounded I/O APIC routing with MADT overrides;
- HPET main-counter runtime;
- AHCI, writable NVMe and writable xHCI USB Mass Storage through the unified
  block-device registry used by shell and installer selection;
- xHCI HID keyboard and mouse runtime, including keyboard hotplug/ring-wrap;
- Intel HDA PCM DMA with AC'97 compatibility;
- MADT AP discovery, INIT/SIPI startup, per-CPU AP stacks/state, Local APIC
  IPIs, cross-CPU work rendezvous and synchronous TLB shootdown;
- firmware-derived FADT/DSDT reset and S5 power methods with existing hardware
  and emulator fallbacks preserved.

The old `smp_scheduler` qualification label was inaccurate: it exercised
cross-CPU work dispatch, not the process/thread scheduler. It is now
`smp_cross_cpu_work`. Scheduler 2.0 and multicore userspace scheduling are
the explicit 6.0 Core Steel scope.

## 5.0 authoritative automated evidence

Exact candidate SHA: `90d5ffc80f91236e10118324c8e3b5d6f4a1c781`.

All required workflows completed successfully:

- Steel Closeout — Actions run `37641425299`;
- HPET Main Counter — Actions run `37641425316`;
- SMP Runtime — Actions run `37641425398`;
- Intel HDA Runtime — Actions run `37641425368`;
- NVMe Block Runtime — Actions run `37641425280`;
- xHCI USB Mass Storage Transport — Actions run `37641425350`;
- xHCI USB Keyboard — Actions run `37641425387`;
- xHCI USB Mouse — Actions run `37641425292`;
- Steel Foundations — Actions run `37641425390`;
- CLA check — Actions run `37641425461`.

The earlier matrix passed, but it did not include xHCI + VirtIO coexistence or
simultaneous USB keyboard + mouse. A later runtime audit reproduced both gaps,
so the 5.0 qualification is reopened. The old green runs remain useful evidence
for the subsystems they actually exercised, but no longer constitute a complete
5.0 Definition of Done.

## Reopened 5.0 blockers

- xHCI and VirtIO-net used the same fixed kernel MMIO virtual window; the audit
  reproduced network fallback when both drivers were active. The repair branch
  moves VirtIO-net to its own window and adds a real coexistence gate.
- xHCI still owns one active slot/device context at a time. Simultaneous USB
  keyboard + mouse therefore remains a 5.0 blocker until per-device contexts
  and a combined runtime gate pass.
- HID Boot Mouse still has no wheel/report-protocol extension; this is tracked
  as a P2 input gap.

The audit's claims that USB Mass Storage, NVMe, SMP and Intel HDA were absent
were correct for its older `2d550f1` baseline, but those subsystems were added
later and remain implemented in the current integration line.

Do not advance dependent 6.0 work as authoritative until the reopened P1 gates
are green.

## Development policy

`AUDIT -> DESIGN -> IMPLEMENT -> BUILD -> TEST -> FIX -> REGRESSION ->
DOCUMENT -> COMMIT -> NEXT`.

Routine commits, pushes and merges are authorized. Do not force-push, rewrite
history, delete branches/tags/releases or perform similarly destructive Git
operations without explicit instruction. Keep Windows PowerShell and Oracle
VirtualBox support intact.

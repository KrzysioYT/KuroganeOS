# KuroganeOS Development State

Last updated: 2026-10-07

## Integration branch

`gpt/road-to-15-consolidation`

Integration HEAD after PR #44:

`653bd33b9611deaaf1330a3e34b3b41c573a9ba9`

Closeout branch: `chatgpt/5.0-closeout`.

## Formal release track

- `3.3.3-dev — Red Flux`: QUALIFIED
- `3.4.0-dev — System Services`: QUALIFIED
- `3.5.0-dev — Connected Userspace`: QUALIFIED
- `3.6.0-dev — Flux Stabilization`: QUALIFIED
- `4.0.0-dev — Pre-Steel`: QUALIFIED
- `5.0.0-dev — Steel / Hardware`: QUALIFICATION CANDIDATE
- `6.0.0-dev` through `14.0.0-rc`: pending
- `15.0.0 stable`: target

## Steel candidate

Implemented scope:

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

## Closeout rule

The exact candidate must pass Steel closeout and the retriggered HPET, SMP,
HDA, NVMe, USB Mass Storage, USB keyboard and USB mouse workflows. Until those
jobs are green, 5.0 remains a candidate rather than a qualified milestone.

User-side VirtualBox/physical-hardware tests begin from the qualified 5.0
candidate and are recorded as external evidence.

## Development policy

`AUDIT -> DESIGN -> IMPLEMENT -> BUILD -> TEST -> FIX -> REGRESSION ->
DOCUMENT -> COMMIT -> NEXT`.

Routine commits, pushes and merges are authorized. Do not force-push, rewrite
history, delete branches/tags/releases or perform similarly destructive Git
operations without explicit instruction. Keep Windows PowerShell and Oracle
VirtualBox support intact.

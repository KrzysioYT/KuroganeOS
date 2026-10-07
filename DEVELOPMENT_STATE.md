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
- `5.0.0-dev — Steel / Hardware`: QUALIFIED (automated matrix)
- `6.0.0-dev` through `14.0.0-rc`: pending
- `15.0.0 stable`: target

`5.0.0-dev` is the formal Steel milestone identity. The `-dev` suffix does
not mean the 5.0 engineering gate is incomplete; only 15.0.0 is planned as the
first product-level STABLE release.

## Steel qualified scope

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

The exact candidate therefore satisfies the automated 5.0 Steel Definition of
Done. Oracle VirtualBox and physical-machine checks remain external validation:
they are not silently converted into PASS, and any real failure reported there
reopens the affected 5.0 subsystem before Core Steel depends on it.

## User validation checkpoint

Use the 5.0 test plan in `docs/testing/STEEL_5_0_MANUAL.md`. The first manual
round should cover boot, install, persistent reboot, networking, input, audio,
power/reboot behavior and serial diagnostics on Oracle VirtualBox. Physical
hardware can then be added as separate evidence.

Development toward 6.0 may continue after this checkpoint, but test failures
from 5.0 take priority over dependent Core Steel work.

## Development policy

`AUDIT -> DESIGN -> IMPLEMENT -> BUILD -> TEST -> FIX -> REGRESSION ->
DOCUMENT -> COMMIT -> NEXT`.

Routine commits, pushes and merges are authorized. Do not force-push, rewrite
history, delete branches/tags/releases or perform similarly destructive Git
operations without explicit instruction. Keep Windows PowerShell and Oracle
VirtualBox support intact.

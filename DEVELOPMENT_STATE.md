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
- `5.0.0-dev — Steel / Hardware`: QUALIFIED AFTER COEXISTENCE AUDIT
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

Qualified post-audit runtime candidate:

`569bae4c33aa0c04db87f0cbe785da1aa45cc084`

The original 5.0 closeout matrix was reopened after an external QEMU audit
found two real configurations that were not covered: xHCI + VirtIO-net and
simultaneous USB keyboard + mouse. Both issues were fixed before
requalification.

Post-audit same-SHA evidence:

- Steel Closeout — Actions run `37661212373` — PASS;
- xHCI + VirtIO coexistence — run `37661212429` — PASS;
- simultaneous xHCI Multi HID — run `37661212451` — PASS;
- xHCI USB Keyboard full ring-wrap/hotplug/late-attach — run
  `37661212449` — PASS;
- xHCI USB Mouse — run `37661212378` — PASS;
- xHCI USB Mass Storage — run `37661212361` — PASS;
- NVMe block runtime — run `37661212483` — PASS;
- Intel HDA runtime — run `37661212532` — PASS;
- HPET main counter — run `37661212464` — PASS;
- Steel Foundations — run `37661212233` — PASS;
- CLA check — run `37661212400` — PASS.

The strengthened Steel Closeout itself now requires a four-vCPU SMP/HPET/ACPI
boot and an audited combined QEMU run with USB keyboard + USB mouse + VirtIO,
real HID input, DHCP and gateway ICMP. The dedicated coexistence workflow also
proves keyboard+VirtIO, mouse+VirtIO and VirtIO polling fallback.

## Audit closure

- **xHCI/VirtIO MMIO collision: FIXED.** VirtIO-net now owns a dedicated
  `0xFFFFB7...` virtual region. A host regression prevents overlap with xHCI
  and the B3 APIC region.
- **one-active-HID xHCI limitation: FIXED for simultaneous keyboard + mouse.**
  xHCI now has an independent companion HID slot with its own contexts, EP0
  ring, interrupt ring, DMA buffer, decoder and lifecycle. Foreign-slot events
  are deferred rather than consumed by synchronous transfers.
- **USB Mass Storage / NVMe / SMP / Intel HDA:** the audit's absence findings
  referred to older source `2d550f1`; these subsystems had been implemented
  later and passed again on the post-audit candidate.
- **mouse wheel:** still a P2 limitation of the Boot Mouse path. Wheel/report
  protocol support is not falsely claimed by the 5.0 gate.

Oracle VirtualBox and physical-machine validation remain external evidence.
Any real failure there reopens the affected subsystem; absence of that external
evidence is never converted into a fake PASS.

## Development policy

`AUDIT -> DESIGN -> IMPLEMENT -> BUILD -> TEST -> FIX -> REGRESSION ->
DOCUMENT -> COMMIT -> NEXT`.

Routine commits, pushes and merges are authorized. Do not force-push, rewrite
history, delete branches/tags/releases or perform similarly destructive Git
operations without explicit instruction. Keep Windows PowerShell and Oracle
VirtualBox support intact.

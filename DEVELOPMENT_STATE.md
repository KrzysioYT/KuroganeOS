# KuroganeOS Development State

Last updated: 2026-09-26

## Integration branch

`gpt/road-to-15-consolidation`

Current qualified integration HEAD after PR #25:

`0376722a5f0076aca9d117b41552f03499b6ac3d`

## Formal release track

- `3.3.3-dev — Red Flux`: QUALIFIED
- `3.4.0-dev — System Services`: QUALIFIED
- `3.5.0-dev — Connected Userspace`: QUALIFIED
- `3.6.0-dev — Flux Stabilization`: QUALIFIED
- `4.0.0-dev — Pre-Steel`: QUALIFIED
- `5.0.0-dev — Steel / Hardware`: ACTIVE, not qualified
- `6.0.0-dev — Core Steel` through `14.0.0-dev — Forge Desktop / RC`: pending
- `15.0.0 stable`: target

The embedded runtime version must not be bumped merely because a subsystem slice
lands. Version advancement follows the formal roadmap and qualification evidence.

## Completed Steel USB slices

- bounded xHCI HID keyboard runtime and full hotplug/ring-wrap regression;
- bounded xHCI HID mouse runtime for one active HID device;
- USB Mass Storage BOT/SCSI protocol foundation — PR #23, merge
  `559c51000150eed0b9e7127e8fd31ae0118be5f9`;
- bounded xHCI bulk endpoint/TRB planning — PR #24, merge
  `6b92d7cd4bf5c48bd7640f784a1565d5fe1e586f`;
- bounded bulk Endpoint Context encoder with exact host regression — PR #25,
  merge `0376722a5f0076aca9d117b41552f03499b6ac3d`.

PR #25 passed CLA, the real xHCI USB Mouse gate and the complete real xHCI USB
Keyboard qualification matrix before merge.

## Current Steel workstream

Branch:

`chatgpt/5.0-usb-storage-enumeration`

Goal: prove real USB Mass Storage enumeration and xHCI Bulk IN/OUT endpoint
configuration before enabling any block I/O.

Current slice intentionally does only:

1. recognize the qualified BOT/SCSI-transparent Mass Storage interface;
2. SET_CONFIGURATION on the real device;
3. configure independent Bulk IN and Bulk OUT transfer rings through production
   xHCI Endpoint Contexts;
4. register the USB child in the Device Model as `Initializing`, never
   `Ready`;
5. qualify the path with a real QEMU `qemu-xhci + usb-storage` device.

It does **not** yet send CBW/data/CSW transactions and does not expose a usable
`storage::block::Device`.

Next dependent slices after this gate passes:

1. bounded synchronous BOT transaction engine with exact transfer completion
   ownership and residue/status validation;
2. INQUIRY / TEST UNIT READY / REQUEST SENSE / READ CAPACITY(10) runtime;
3. read-only `storage::block::Device` qualification;
4. WRITE(10) + SYNCHRONIZE CACHE with failure recovery;
5. hot-remove and stale-handle cleanup;
6. unified storage registry so AHCI, USB Mass Storage and later NVMe feed the
   same storage/installer path instead of AHCI-specific enumeration.

## Hard 5.0 gaps found by audit

The current source tree does not yet contain complete implementations for:

- NVMe;
- SMP AP startup, per-CPU stacks/state, SMP scheduling and TLB shootdown;
- Intel HDA;
- HPET;
- USB hubs / simultaneous HID devices / physical-hardware USB qualification.

Existing foundations that should be extended rather than replaced:

- MADT processor discovery;
- Local APIC and I/O APIC routing;
- MSI/MSI-X infrastructure;
- AHCI and the synchronous `storage::block::Device` ABI;
- E1000, PCnet and VirtIO-net;
- AC'97 audio;
- Device/Driver Model.

Steel must not be marked qualified until the formal 5.0 roadmap gates are
satisfied with runtime evidence.

## Development policy

- Use `AUDIT -> DESIGN -> IMPLEMENT -> BUILD -> TEST -> FIX -> REGRESSION ->
  DOCUMENT -> COMMIT -> NEXT`.
- Work in small reviewable slices.
- A real failure blocks dependent work until fixed.
- Run available host and real runtime qualification before merging.
- Routine commits, pushes and merges are authorized without asking each time.
- Do not force-push, rewrite history, delete branches/tags/releases or perform
  similarly destructive Git operations without explicit user instruction.
- Keep Windows PowerShell and Oracle VirtualBox support intact.

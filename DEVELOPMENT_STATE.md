# KuroganeOS Development State

Last updated: 2026-09-26

## Integration branch

`gpt/road-to-15-consolidation`

Current qualified integration HEAD after PR #30:

`d1d3d1e1194b01d747932097a0ce79bd44dfeca8`

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
  merge `0376722a5f0076aca9d117b41552f03499b6ac3d`;
- real xHCI USB Mass Storage enumeration and independent Bulk IN/OUT endpoint
  configuration — PR #26, merge
  `07ebba7f70d297d3ded1704475bb50f033ccf71c`;
- exact xHCI Transfer Event ownership validation for bulk traffic — PR #27,
  merge `08d8d76949fc6aa2bf065dff2c42b9aa46affbe8`;
- first real BOT TEST UNIT READY CBW/CSW exchange — PR #28, merge
  `83658f82b6d8e10f57ed8b22d0127661b3d33b10`.

PR #26 candidate `dc8fbaaed40534cb7346d4e3959f7472057881a1` passed:
- Mass Storage real-QEMU run `36213602627`;
- USB Mouse regression run `36213602647`;
- complete USB Keyboard regression run `36213602671`;
- CLA run `36213602651`.

The USB child remains `Initializing`; no usable block I/O is claimed.

## Current Steel workstream

Branch:

`chatgpt/5.0-usb-storage-geometry`

Goal: move the proven BOT transport into the first real SCSI device
qualification without exposing block I/O yet.

Current slice handles a legal initial CHECK CONDITION with REQUEST SENSE,
retries TEST UNIT READY, requires INQUIRY and READ CAPACITY(10), and publishes
validated 512..4096-byte geometry only after exact BOT/CSW validation.

Next dependent slices after this gate passes:

1. extend the proven BOT transport with INQUIRY / REQUEST SENSE /
   READ CAPACITY(10) data stages;
2. expose qualified read-only `storage::block::Device` geometry and READ(10);
3. read-only storage qualification against scratch USB media;
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

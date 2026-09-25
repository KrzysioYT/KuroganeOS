# KuroganeOS Development State

Last updated: 2026-09-25

## Integration branch

`gpt/road-to-15-consolidation`

Current integration HEAD after PR #23:

`559c51000150eed0b9e7127e8fd31ae0118be5f9`

## Active release track

- `3.3.3-dev — Red Flux`: qualified scoped DEV milestone.
- `3.4.0-dev — System Services`: qualified.
- `4.0.0-dev — Pre-Steel`: qualified.
- `5.0.0-dev — Steel / Hardware`: ACTIVE, not qualified.
- Target: progress conservatively through later milestones toward `15.0.0 stable`.

The embedded runtime version is not to be bumped merely because a subsystem slice lands. Version advancement must follow the release roadmap and qualification evidence.

## Recently completed Steel slices

- bounded xHCI HID keyboard runtime and regression matrix;
- bounded xHCI HID mouse runtime for one active HID device;
- repaired keyboard qualification after HID generalization;
- USB Mass Storage protocol foundation:
  - BOT interface parser;
  - CBW encoder / CSW decoder;
  - bounded SCSI CDB builders;
  - READ CAPACITY(10) parser;
  - host regression coverage.

PR #23 was merged into the integration branch as `559c51000150eed0b9e7127e8fd31ae0118be5f9`.

## Current constraints

The xHCI runtime is intentionally bounded. It does not yet claim:

- simultaneous keyboard + mouse;
- USB hubs;
- report-protocol wheel extensions;
- USB Mass Storage runtime;
- physical-hardware USB qualification.

The Mass Storage protocol layer exists, but there is no production xHCI bulk endpoint runtime and no registration as `BlockDeviceOps`.

## Current workstream

Branch:

`chatgpt/5.0-xhci-bulk-foundation`

Goal: add a small, bounded xHCI bulk-transfer foundation before attempting a complete USB storage driver.

Planned sequence:

1. isolate generic transfer-ring completion/accounting needed by non-HID endpoints;
2. add bounded bulk-IN / bulk-OUT endpoint configuration contracts;
3. add host-testable validation/accounting before activating production runtime;
4. qualify the slice;
5. only then connect BOT/SCSI transactions;
6. only after transport qualification expose USB storage through the existing `BlockDeviceOps` contract.

## Development policy

- Work in small reviewable slices.
- Run available host/CI qualification before merging.
- Normal commits, pushes and merges are authorized without asking each time.
- Do not force-push, rewrite history, delete branches/tags/releases, or perform similarly destructive Git operations without explicit user instruction.
- Keep Windows PowerShell and Oracle VirtualBox support intact.

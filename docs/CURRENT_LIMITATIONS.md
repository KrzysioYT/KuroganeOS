# KuroganeOS current limitations

This document describes current capability limits at the active Road-to-15 HEAD. It is not a list of missing requirements for already-qualified earlier milestones.

## Formal milestone state

- `3.3.3-dev — Red Flux`: **QUALIFIED for its bounded DEV scope**.
- `3.4.0-dev — System Services`: **IN DEVELOPMENT**.
- The current `dev/3.4.1-event-broker` suffix is an internal System Services workstream, not a separate formal version.

## External compatibility validation

Oracle VirtualBox host acceptance is **OPTIONAL / EXTERNAL VALIDATION**. The repository retains VirtualBox-oriented media and harness tooling, but unavailable Oracle VirtualBox execution is not a FAIL, not a Definition-of-Done blocker and is excluded from progress percentages.

Automated qualification is based on evidence that can actually be executed and inspected: host tests, kernel/ABI/SDK tests, media/FAT32/VFS checks, OVMF/QEMU boot, QEMU network matrices and real guest TLS/HTTPS workflows.

## Architecture and security limits

- x86-64 UEFI only.
- Final general process isolation/security policy is not complete; Iron Shield owns that scope.
- `FNV1A64-DEV` remains a development-only password verifier and must be replaced by Argon2id or an equivalent secure KDF in Iron Shield.
- SMP is not yet a qualified production capability.
- There is no final updater/rollback/recovery stack yet.

## Storage and filesystem limits

- FAT32 is the current persistent filesystem foundation.
- AHCI/SATA is the primary qualified persistent block path.
- NVMe production support and USB/xHCI mass-storage parity belong to Steel / Hardware.
- File-backed mmap, full users/groups/ACL and links are not present yet.

## Graphics limits

- Red Flux uses UEFI GOP scanout with software rendering/compositing.
- Hardware 3D command submission is not enabled and is not claimed.
- Real GPU resources, queues, shaders and swapchain work belong to `9.0.0-dev — Forge Graphics`.
- Direct3D compatibility may not be claimed until a real backend performs actual work.

The lack of hardware 3D does not reduce the completed Red Flux percentage because software graphics are the defined Red Flux scope.

## Audio limits

- Red Flux has a bounded AC'97 PCM foundation and Ring-3-facing audio primitives.
- A full async audio service, mixer, capture/resampling and Intel HDA path are later Connected Userspace / Steel scope.

Those later capabilities are not missing Red Flux Definition-of-Done items.

## Network limits

- Red Flux qualified scope is IPv4/DHCP/DNS/TCP plus real TLS/HTTPS on the current guest stack.
- Public async socket APIs belong to Connected Userspace.
- IPv6 and Network Service 2.0 belong to Connected Steel.
- Wi-Fi hardware support is not currently qualified.

## Desktop limits

- Red Flux is a software-rendered classic desktop, not the final Forge Desktop.
- HiDPI, richer Unicode/font rendering, final motion/design system, clipboard and multi-monitor work remain future scope.

## System Services limits

The `3.4.0-dev` foundation already has real named IPC registration/discovery, PID ownership, generation-safe handles, process-exit cleanup and a public Service SDK.

Current Event Broker state:
- real `events.v1` Ring-3 endpoint: **IMPLEMENTED** and starts successfully;
- bounded subscriptions and unsubscribe logic: **IMPLEMENTED**;
- kernel event create/grant/signal path: **IMPLEMENTED**;
- public Event Broker protocol/ABI: **IMPLEMENTED**;
- real Ring-3 subscribe → publish → wait → unsubscribe qualification: **FAIL** in Actions run `33221674569`, therefore **NOT QUALIFIED**.

Settings, notifications, accounts, service-based session lifecycle, clipboard and service restart/recovery remain future `3.4.0-dev` work.

## Reliability rule

Compilation or media creation alone is never runtime qualification. Real runtime FAIL stays FAIL until fixed and rerun. Unavailable external environments remain optional/unverified rather than fabricated as PASS or used as artificial blockers.

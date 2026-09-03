# KuroganeOS Version History

This document tracks the architectural generations of KuroganeOS. Release-specific technical truth belongs in `docs/releases/<version>.md`; this file records the evolution and release-line intent.

## 3.x — Red Flux

State: active closeout generation.

Red Flux established the current native x86-64 UEFI kernel/userspace system: Ring 3, ELF64 applications, syscalls, process lifecycle, writable FAT32/VFS, AHCI/GPT installation, the current desktop, networking foundations and AC'97 output. The remaining 3.x work is stabilization, userspace services/network I/O foundations and removal of avoidable Red Flux coupling before architecture migration.

Current audited baseline: `3.3.3-dev` at `17bd55091c63544b9585840192f0eb288e9cffff`.

## 4.x — Pre-Steel

Purpose: formalize Device Model 2.0, Driver Manager 2.0, kernel/driver/userspace boundaries, structured status/error handling, capabilities, ownership and diagnostics before the Steel generation.

## 5.x — Steel Foundation

Purpose: hardware and driver generation. Device lifecycle, PCI/PCIe, ACPI/APIC, SMP, common storage, hardened AHCI, real NVMe, xHCI/USB, Intel HDA foundations and hardware qualification.

## 6.x — Core Steel

Purpose: Kernel Core 2.0, hardened PMM/VMM, SMP-aware scheduling, threads/processes/jobs, IPC 2.0, shared synchronization primitives, VFS 2.0 and syscall ABI qualification.

## 7.x — Iron Shield

Purpose: highest-priority security/reliability generation. Threat model, syscall/user-pointer hardening, process/address-space isolation, identity/permissions, secure credentials/KDF, capability sandbox, watchdog/crash recovery, fuzzing and stress qualification.

Security fixes are never deferred until 7.x when a critical issue is discovered earlier.

## 8.x — Connected Steel

Purpose: Network Service 2.0, async sockets, hardened IPv4, IPv6, DHCP/DNS services, TLS 1.3, CA/certificate validation, HTTPS/secure socket APIs and network security qualification.

## 9.x — Forge Graphics

Purpose: real Forge Graphics API, resources, commands, synchronization, presentation, shader pipeline, real software 3D rasterization and later real hardware acceleration. Direct3D compatibility may exist only as an honest layer over a working Forge Graphics backend.

## 10.x — Steel Applications

Purpose: Application Runtime 2.0 and core applications: Kurosh 2.0, Vault, Performance, Forge Control, Pulse backend and Kurogane Web shell, with isolation and lifecycle qualification.

## 11.x — Anvil Ecosystem

Purpose: package format/manifests/repositories, dependency resolution, SHA-256/signatures, transactional operations, update/upgrade integration and Kurogane SDK. Official repository target: `repo.kuroganeos.dev`.

## 12.x — Kurogane Platform / Web

Purpose: native web-engine architecture, HTTP/HTTPS navigation, HTML/DOM, CSS/layout/Flexbox/Grid, images/media groundwork, JavaScript foundation, JS-DOM integration and browser security/qualification.

## 13.x — Forge Design

Purpose: centralized Forge design system. Tokens, palette, typography, spacing, industrial blade/forged geometry, shared controls, iconography and motion. This is where final style work begins.

## 14.x — Forge Release Preparation

Purpose: final Forge Desktop shell and user experience, then feature freeze, beta and release-candidate qualification. `14.0.7-beta` freezes major features; `14.0.8-rc` performs broad qualification; `14.0.9-rc` accepts release blockers only.

## 15.0.0 — First Official Steel Release

Channel: `STABLE`.

Meaning: the first officially supported KuroganeOS release after boot/install/recovery, security, hardware, networking, graphics, applications, package/update platform and reliability gates are actually satisfied on QEMU, VirtualBox and at least one supported real-hardware class.

`15.0.0` is never reached by version-number advancement alone.

**Built in Steel. Refined in Fire.**

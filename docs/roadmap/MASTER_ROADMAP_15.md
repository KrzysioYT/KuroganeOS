# KuroganeOS Master Roadmap to 15.0.0

Status: ACTIVE
Final target: `15.0.0` — first official STABLE release.

## Truth and qualification policy

KuroganeOS is a native x86-64 UEFI operating system with its own kernel, Ring-3 userspace, ELF64 applications, syscall ABI, drivers and system services. No fake backend, fake PASS or stub that pretends to complete production work is acceptable.

Progress is counted only from verifiable implementation and evidence: code, host/kernel tests, ABI/SDK tests, regression tests, IMG/ISO generation, FAT32/VFS validation, OVMF/QEMU runtime, networking/TLS runtime and completed GitHub Actions results.

Oracle VirtualBox host acceptance is `OPTIONAL / EXTERNAL VALIDATION`. It is not a Definition-of-Done item, a percentage input or a blocker for formal milestone progression. If an environment cannot be executed, its state stays external/unverified rather than being converted to PASS or FAIL.

## Formal versioning model

The Road to 15 has exactly these formal milestones:

| Formal milestone | Generation / focus |
|---|---|
| `3.3.3-dev` | Red Flux |
| `3.4.0-dev` | System Services |
| `3.5.0-dev` | Connected Userspace |
| `3.6.0-dev` | Flux Stabilization |
| `4.0.0-dev` | Pre-Steel |
| `5.0.0-dev` | Steel / Hardware |
| `6.0.0-dev` | Core Steel |
| `7.0.0-dev` | Iron Shield |
| `8.0.0-dev` | Connected Steel |
| `9.0.0-dev` | Forge Graphics |
| `10.0.0-dev` | Steel Applications |
| `11.0.0-dev` | Anvil |
| `12.0.0-dev` | Platform / Web |
| `13.0.0-dev` | Forge Design |
| `14.0.0-rc` | Forge Desktop / release candidate |
| `15.0.0` | STABLE |

Names such as `3.3.5`, `3.3.9` or `3.4.1` may remain in branch/history names as **internal development workstreams only**. They are not separate formal product versions or release gates.

## 3.3.3-dev — Red Flux

Status: **QUALIFIED (scoped DEV milestone)**.

Historical workstreams folded into 3.3.3-dev:
- installer reliability and persistent profile/locale state;
- network stabilization;
- TLS foundation and real HTTPS qualification;
- userspace I/O and resource ownership cleanup;
- closeout/regression;
- VirtualBox harness work retained as optional external compatibility tooling.

Red Flux DoD covers UEFI/OVMF boot, Try/install media, persistent FAT32 root, recoverable installer state, Ring-3 ELF64/syscalls, bounded filesystem I/O, IPv4/DHCP/DNS/TCP, real TLS/HTTPS, Red Flux login/desktop, bounded AC'97 PCM, GOP/software graphics and full closeout regression.

Hardware 3D, HDA/mixer service, SMP, NVMe parity, final security isolation, updater/recovery and package infrastructure belong to later formal milestones and do not reduce Red Flux completion.

## 3.4.0-dev — System Services

Status: **IN DEVELOPMENT**.

Scope:
- Service Core: registration/unregister, discovery/lookup, PID ownership, unique names, lifecycle/state, metadata and version negotiation;
- service-oriented IPC: request/reply, async messages, endpoint discovery, lifecycle and truthful failure propagation;
- Event Broker: subscriptions/unsubscribe, dispatch, waitable events, async delivery, ownership and cleanup;
- Settings Service: persistent per-user/system settings, read/write and change notifications;
- Notification Service: app/system notifications, type/priority metadata and lifecycle;
- Account Service: account lookup/profile/identity and public API;
- Session Service: login/session creation/ownership/termination, Home and app association;
- clean public filesystem service API for stat/readdir/create/unlink/rename/mkdir/rmdir;
- clipboard and desktop-neutral services;
- service crash detection, stale cleanup, restart/recovery foundation and stress tests;
- SDK: `service.h`, `event_broker.h`, helpers, stable contracts, examples and docs;
- full host/ABI/SDK/Ring-3/IPC/event/media/OVMF/QEMU qualification.

Current priority: Event Broker real Ring-3 roundtrip. Service Architecture base is already qualified; Event Broker remains NOT QUALIFIED until the current runtime failure is fixed and rerun.

## 3.5.0-dev — Connected Userspace

Public socket ABI, async UDP/TCP/DNS, network status/events, userspace TLS/HTTPS, async audio service, application registry/manifests and connected-userspace regression.

## 3.6.0-dev — Flux Stabilization

Compositor cleanup, per-window surfaces, damage regions, input/focus, drag/resize, process/window ownership, app crash isolation, boot/login/session reliability and long-runtime qualification.

## 4.0.0-dev — Pre-Steel

Device Model 2.0, Driver Manager 2.0, kernel/driver boundaries, userspace/device boundaries, unified error/status model, capability foundation, process resource ownership, driver failure isolation and structured boot diagnostics.

## 5.0.0-dev — Steel / Hardware

PCI/PCIe BAR/capabilities/MSI/MSI-X, ACPI/APIC/HPET/power/interrupt routing, SMP/per-CPU state, AHCI/block hardening, NVMe, USB Core/xHCI/enumeration, USB HID/mass storage, Intel HDA/AC'97 compatibility and unified NIC interface.

## 6.0.0-dev — Core Steel

Kernel Core 2.0, PMM/VMM/address spaces, Scheduler 2.0, threads, processes/jobs, IPC 2.0, shared memory/synchronization, VFS 2.0 and syscall ABI qualification.

## 7.0.0-dev — Iron Shield

Security architecture, user/kernel memory validation, process isolation, users/groups, permissions/ACL, secure credential store, Argon2id or equivalent secure KDF replacing `FNV1A64-DEV`, application permissions/capabilities, watchdog/crash recovery and security fuzz/stress qualification.

## 8.0.0-dev — Connected Steel

Network Service 2.0, async sockets, IPv4 hardening, IPv6, DHCP/DNS service, routing/configuration, TLS 1.3, CA store/certificate validation, HTTPS API and firewall foundation.

## 9.0.0-dev — Forge Graphics

Forge Graphics API/device/resources, buffers/textures, command buffers, queues/fences, swapchain/presentation, shader foundation, real software 3D rasterizer, hardware GPU backend foundation and compositor/telemetry integration. GPU/D3D may never be marked working without real backend execution.

## 10.0.0-dev — Steel Applications

Application Runtime 2.0, manifests/permissions/lifecycle, Kurosh 2.0, Vault, Performance, Forge Control foundation, Pulse backend, Kurogane Web shell and application crash/restart qualification.

## 11.0.0-dev — Anvil

Package format/manifests/versioning/repository metadata, sync/search/info/install/remove, dependency resolution, SHA-256/signatures, transactional install/rollback, update/upgrade/system updates and SDK commands `kurogane new/build/run/package`.

## 12.0.0-dev — Platform / Web

Web Engine architecture, HTTP/HTTPS navigation, HTML/DOM, CSS/layout, Flexbox, Grid/text, images/media, JavaScript foundation, JS↔DOM/cookies/storage/history/tabs and browser security/developer console qualification.

## 13.0.0-dev — Forge Design

Design tokens, palette, typography, spacing/layout, blade/steel geometry, controls, lists/trees/tabs/context menus, window/component styling, icons/motion and migration of applications to the unified Forge Design system.

## 14.0.0-rc — Forge Desktop

Final Forge shell, Kurogane Spine, Blade Launcher, Pulse, Vault + Forge Control, Anvil/Kurosh/Web integration, notifications/login/lock/power/session/workspace UX, feature freeze, release-candidate hardening, security/install/update/recovery/uptime/stress evidence and final release media/docs.

## 15.0.0 — STABLE

15.0.0 must be a system that can **BOOT, INSTALL, USE, SECURE, UPDATE, RECOVER and DEVELOP FOR**. Required capabilities include stable UEFI boot, Login→Forge Desktop, Try/install/persistence, updater/recovery, isolation/security/watchdog, SMP, AHCI/NVMe/USB, audio, IPv4+IPv6, TLS/HTTPS, Forge Graphics/compositor, core apps, Anvil, Web, SDK and package repositories. No critical fake implementation is acceptable.

## Workflow

For every atomic work item:

`AUDIT -> DESIGN -> IMPLEMENT -> BUILD -> TEST -> FIX -> REGRESSION -> DOCUMENT -> COMMIT -> NEXT`

A real FAIL is fixed before dependent work continues. An unavailable external environment is documented but does not stop independent engineering.

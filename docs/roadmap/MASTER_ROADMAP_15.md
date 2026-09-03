# KuroganeOS Master Roadmap to 15.0.0

Status: ACTIVE
Final target: `15.0.0` — first official STABLE release.

## Truth and qualification policy

KuroganeOS is a native x86-64 UEFI operating system with its own kernel, Ring-3 userspace, ELF64 applications, syscall ABI, drivers and system services. No fake backend, fake PASS or stub that pretends to complete production work is acceptable.

Progress is counted only from verifiable implementation and evidence: code, host/kernel tests, ABI/SDK tests, regression tests, IMG/ISO generation, FAT32/VFS validation, OVMF/QEMU runtime, networking/TLS runtime and completed GitHub Actions results.

Oracle VirtualBox host acceptance is `OPTIONAL / EXTERNAL VALIDATION`. It is not a Definition-of-Done item, a percentage input or a blocker for formal milestone progression. If an environment cannot be executed, its state stays external/unverified rather than being converted to PASS or FAIL.

The compiled runtime version may remain `3.3.3-dev` while later engineering milestones are qualified. Roadmap qualification does not silently rewrite the version embedded in already-built media.

## Formal versioning model

| Formal milestone | Generation / focus | Current state |
|---|---|---|
| `3.3.3-dev` | Red Flux | QUALIFIED |
| `3.4.0-dev` | System Services | QUALIFIED |
| `3.5.0-dev` | Connected Userspace | QUALIFIED |
| `3.6.0-dev` | Flux Stabilization | QUALIFIED |
| `4.0.0-dev` | Pre-Steel | ACTIVE |
| `5.0.0-dev` | Steel / Hardware | PENDING |
| `6.0.0-dev` | Core Steel | PENDING |
| `7.0.0-dev` | Iron Shield | PENDING |
| `8.0.0-dev` | Connected Steel | PENDING |
| `9.0.0-dev` | Forge Graphics | PENDING |
| `10.0.0-dev` | Steel Applications | PENDING |
| `11.0.0-dev` | Anvil | PENDING |
| `12.0.0-dev` | Platform / Web | PENDING |
| `13.0.0-dev` | Forge Design | PENDING |
| `14.0.0-rc` | Forge Desktop / release candidate | PENDING |
| `15.0.0` | STABLE | FINAL TARGET |

Names such as `3.3.5`, `3.3.9` or `3.4.1` may remain in branch/history names as **internal development workstreams only**. They are not separate formal product versions or release gates.

## 3.3.3-dev — Red Flux

Status: **QUALIFIED (scoped DEV milestone)**.

Red Flux DoD covers UEFI/OVMF boot, Try/install media, persistent FAT32 root, recoverable installer state, Ring-3 ELF64/syscalls, bounded filesystem I/O, IPv4/DHCP/DNS/TCP, real TLS/HTTPS, Red Flux login/desktop, bounded AC'97 PCM, GOP/software graphics and full closeout regression.

The reopened Kurogane Fatal Diagnostic MUST HAVE is also qualified. It remains kernel-owned, heap-independent after the fatal transition, userspace/PNG-independent, with real exception/register/process state, bounded kernel event history, serial mirror and nested fallback. Actions run `33315953767` passed after the 3.4 runtime stack fix, proving that System Services work did not regress this Red Flux release gate.

Hardware 3D, HDA/mixer service, SMP, NVMe parity, final security isolation, updater/recovery and package infrastructure belong to later formal milestones and do not reduce Red Flux completion.

## 3.4.0-dev — System Services

Status: **QUALIFIED**.

Completed scope:
- named IPC registration, unregister, discovery and endpoint lookup;
- PID ownership, generation-safe handles and process-exit cleanup;
- service metadata and version negotiation;
- Event Broker subscribe/unsubscribe/publish/wait/wakeup integrated with Ring-3 scheduling;
- typed persistent Settings Service and change notifications;
- Notification Service lifecycle/roundtrip/liveness;
- Account Service lifecycle/roundtrip/liveness;
- Session Service ownership, Login integration, roundtrip/liveness;
- Clipboard Service bounded state, ownership, lifecycle and recovery;
- public persistent FAT32/VFS filesystem API;
- stale endpoint protection and service restart/rebind foundation;
- 256-iteration service-channel churn qualification;
- host/kernel/VFS/IPC/TCP/SDK regression;
- clean release media and OVMF/QEMU combined-runtime qualification.

### 3.4 root-cause closure

The historical `fsprobe` closeout panic was caused by kernel stack ownership, not by filesystem data corruption or a deliberate kernel `ud2`. The fatal `RIP=0xA3C01` was outside the loaded PIE kernel. Diagnostics resolved the expected saved return to normalized `0x4A678`, immediately after `x86_64_enter_user` in `user::runtime::run()`, and proved that the saved return had been overwritten to zero after the deep FAT32 probe.

Commit `66fffaf225447261abc264500d5cf6f36165e7b9` restores the invariant by enlarging the per-thread kernel stack to 96 KiB and separating a 64 KiB Ring-3 syscall/IRQ entry area from the retained 32 KiB suspended launch chain.

A later closeout timeout was a test-harness process-table overcommit. Commit `76ac39e4421a1ae0ba720f46b040de10cf9e2096` serializes and reaps the real one-shot probes while retaining bounded `MAX_PROCESSES=16` / `MAX_THREADS=16` production limits and the full required marker contract.

Authoritative evidence:
- Event Broker: `33315953868` PASS;
- Settings persistence: `33315953774` PASS;
- Notification lifecycle: `33315953760` PASS;
- Fatal Diagnostic regression: `33315953767` PASS;
- combined System Services closeout: `33317140601` PASS;
- full 3.4 regression sweep: `33317520153` PASS.

No known 3.4 blocker remains after the combined closeout and regression sweep.

## 3.5.0-dev — Connected Userspace

Status: **QUALIFIED**.

Completed scope:
- bounded public process-owned socket table with generation-safe handles and explicit PID ownership;
- deterministic socket/process-exit cleanup and stale-handle protection;
- public `socket` / `close` / `bind` / `connect` / `send` / `recv` transport contracts;
- real UDP roundtrip, bounded receive state and waitable readiness;
- real TCP connect/progression/refused/reset/timeout/cleanup qualification;
- DNS Service integration, NXDOMAIN/malformed handling and service restart/rebind;
- live network status/events from E1000 carrier state through `netevtd` and Event Broker to Ring-3;
- verified TLS/HTTPS runtime with CA validation, hostname validation, SNI and bounded responses;
- asynchronous Audio Service with bounded queues, multi-client mixing, generation-safe streams and process-exit AC'97 ownership cleanup;
- Application Registry with bounded catalog, manifests, executable validation and client cleanup;
- same-SHA connected-userspace component regression plus clean production KVM closeout.

### 3.5 authoritative evidence

All component gates below were re-run against exact source SHA `7f715a9d654a76b300f1161ba86f4e97fee5e500`:
- Socket/TCP core: Actions run `33410591776` — PASS;
- DNS Service: Actions run `33410593584` — PASS;
- Network Events: Actions run `33410595658` — PASS;
- Audio Service + App Registry KVM cross-qualification: Actions run `33410597347` — PASS;
- verified TLS/HTTPS runtime: Actions run `33410598935` — PASS;
- full 3.4 regression sweep on the 3.5 closeout SHA: Actions run `33410600879` — PASS.

Connected Userspace closeout: Actions run `33410583405` — **PASS**. Its final self-hosted KVM job `99549667506` performed the full host regression suite, a clean release IMG/ISO build and an uninjected production OVMF/q35/KVM boot with E1000 and Intel ICH AC'97. The runtime reached `[TEST] dhcp_lease: PASS`, `[TEST] network_gateway_icmp: PASS`, `[TEST] ALL_REQUIRED_TESTS_PASSED`, real AC'97 initialization and `[TEST] connected_userspace_closeout: PASS`.

No known 3.5 blocker remains after the same-SHA component gates and clean KVM production closeout.

## 3.6.0-dev — Flux Stabilization

Status: **QUALIFIED**.

Preserve the already-working Red Flux Window Core: generation-checked window IDs, focus/z-order, header drag, interactive resize, minimize/maximize/restore/close, Alt+Tab/Alt+F4, software pointer, session ownership and the existing full-frame software backbuffer.

### Work order

1. Native bounded per-window surfaces rather than relying only on full `KU_SYS_UI_PRESENT` frame transport.
2. Damage-region tracking, clipping and partial composition with deterministic full-frame fallback.
3. Harden focus/input routing, drag/resize state and window/process ownership across teardown.
4. Prove app crash isolation: a crashed GUI owner must release its windows/surfaces without taking down the session.
5. Harden Login → Home → Login supervision and session restart/recovery.
6. Long-runtime desktop churn qualification covering repeated create/present/focus/resize/close/crash/relaunch cycles.
7. Full host/SDK/media regressions plus real OVMF/KVM Flux Stabilization closeout.

3.6 does not claim GPU acceleration; Forge Graphics remains a later formal milestone.

### 3.6 authoritative evidence

Flux Stabilization was qualified at exact source SHA `0caf8cc42f872b11b44f874029eb41aeae152abc`:
- Flux Runtime Core: Actions run `33530401377` — PASS;
- Flux Session Recovery: Actions run `33530403709` — PASS;
- 3.4 regression sweep: Actions run `33530406070` — PASS;
- 3.5 Connected Userspace closeout: Actions run `33530408164` — PASS;
- authoritative Flux Stabilization closeout: Actions run `33530392489` — **PASS**.

The closeout's host-release, same-SHA dispatch and final evidence jobs all passed. No known 3.6 blocker remains.

## 4.0.0-dev — Pre-Steel

Status: **ACTIVE**.

Device Model 2.0, Driver Manager 2.0, kernel/driver boundaries, userspace/device boundaries, unified error/status model, capability foundation, process resource ownership, driver failure isolation and structured boot diagnostics.

## 5.0.0-dev — Steel / Hardware

PCI/PCIe BAR/capabilities/MSI/MSI-X, ACPI/APIC/HPET/power/interrupt routing, AHCI/block hardening, NVMe, USB Core/xHCI/enumeration, USB HID/mass storage, Intel HDA/AC'97 compatibility and unified NIC interface.

SMP is complete only when ACPI/MADT CPU discovery, AP startup, per-CPU state/stacks, interrupt routing, synchronization/locking, SMP scheduler, TLB shootdown, per-CPU kernel data and multi-CPU runtime qualification all work. MADT enumeration alone is not SMP.

## 6.0.0-dev — Core Steel

Kernel Core 2.0, PMM/VMM/address spaces, Scheduler 2.0, threads, processes/jobs, IPC 2.0, shared memory/synchronization, VFS 2.0 and syscall ABI qualification.

## 7.0.0-dev — Iron Shield

Security architecture, user/kernel memory validation, process isolation, users/groups, permissions/ACL, secure credential store, Argon2id or equivalent secure KDF replacing `FNV1A64-DEV`, application permissions/capabilities, watchdog/crash recovery and security fuzz/stress qualification.

## 8.0.0-dev — Connected Steel

Network Service 2.0, async sockets hardening, IPv4 hardening, IPv6, DHCP/DNS service, routing/configuration, TLS 1.3, CA store/certificate validation, HTTPS API and firewall foundation.

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

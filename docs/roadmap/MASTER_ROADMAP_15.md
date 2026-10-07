# KuroganeOS Master Roadmap to 15.0.0

Status: ACTIVE
Final target: `15.0.0` — first official STABLE release.

## Truth and qualification policy

KuroganeOS is a native x86-64 UEFI operating system with its own kernel, Ring-3 userspace, ELF64 applications, syscall ABI, drivers and system services. No fake backend, fake PASS or stub that pretends to complete production work is acceptable.

Progress is counted only from verifiable implementation and evidence: code, host/kernel tests, ABI/SDK tests, regression tests, IMG/ISO generation, FAT32/VFS validation, OVMF/QEMU runtime, networking/TLS runtime and completed GitHub Actions results.

Oracle VirtualBox host acceptance is `OPTIONAL / EXTERNAL VALIDATION`. It is not a Definition-of-Done item, a percentage input or a blocker for formal milestone progression. If an environment cannot be executed, its state stays external/unverified rather than being converted to PASS or FAIL.

The compiled runtime identity follows explicit milestone closeout. Roadmap qualification never silently rewrites already-built media, and only `15.0.0` is planned as the first product-level STABLE release.

## Formal versioning model

| Formal milestone | Generation / focus | Current state |
|---|---|---|
| `3.3.3-dev` | Red Flux | QUALIFIED |
| `3.4.0-dev` | System Services | QUALIFIED |
| `3.5.0-dev` | Connected Userspace | QUALIFIED |
| `3.6.0-dev` | Flux Stabilization | QUALIFIED |
| `4.0.0-dev` | Pre-Steel | QUALIFIED |
| `5.0.0-dev` | Steel / Hardware | QUALIFIED AFTER AUDIT |
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

Status: **QUALIFIED** at source SHA `bbec12248773930d6f40aa98837ff13e99b7cf5e`.

Device Model 2.0, Driver Manager 2.0, kernel/driver boundaries, userspace/device boundaries, unified error/status model, capability foundation, process resource ownership, driver failure isolation and structured boot diagnostics.

The authoritative same-SHA closeout is Actions run `34260827773`; final job `102186252372` recorded `KuroganeOS 4.0 Pre-Steel closeout: PASS sha=bbec12248773930d6f40aa98837ff13e99b7cf5e`. The matrix included host/media production boot, Device/Driver/Ring-3 device ABI, KuroFS native two-boot persistence, Fatal Diagnostic, network/TLS, qualified 3.4/3.5 regressions, Flux, audio/application and Unified Status gates.

## 5.0.0-dev — Steel / Hardware

Status: **QUALIFIED AFTER COEXISTENCE AUDIT**.

Steel's code-complete scope is the bounded hardware substrate required before
Core Steel: PCI/PCIe BAR and capability validation, MSI/MSI-X and I/O APIC
routing, ACPI table discovery, HPET, firmware-derived ACPI reset/S5 power,
AHCI/NVMe/USB block I/O, xHCI HID keyboard/mouse, Intel HDA with AC'97
compatibility, unified storage/NIC selection, and the first multi-CPU kernel
execution substrate.

The SMP scope is intentionally precise: MADT CPU discovery, INIT/SIPI AP
startup, per-CPU AP stacks/state, shared IDT + Local APIC IPI delivery,
bounded cross-CPU work rendezvous, synchronous TLB shootdown and a real
four-vCPU runtime gate. The marker is `smp_cross_cpu_work`; it must never be
reported as a full process scheduler. **Scheduler 2.0, userspace load balancing,
per-CPU run queues and the wider thread/process redesign belong to 6.0 Core
Steel.**

Qualified component evidence before closeout includes MSI/MSI-X transport,
production VirtIO-net interrupt adoption, xHCI keyboard hotplug/ring-wrap,
xHCI mouse, writable USB Mass Storage, NVMe namespace read/write/flush, HPET
main-counter progress and Intel HDA PCM DMA. PR #44 added the four-vCPU
AP/cross-CPU/TLB runtime and merged as
`653bd33b9611deaaf1330a3e34b3b41c573a9ba9`.

The final 5.0 candidate adds FADT/DSDT power discovery and routes shell
reboot/poweroff through ACPI before the existing platform fallbacks. The
`Qualify 5.0 Steel Closeout` workflow requires the exact `5.0.0-dev`
candidate to pass full host regression, a clean release-media build, ACPI
power discovery, HPET and four-vCPU SMP boot. The shared host-test change also
retriggers HDA, NVMe, USB Mass Storage, USB keyboard and USB mouse gates.

The exact candidate `90d5ffc80f91236e10118324c8e3b5d6f4a1c781` passed the then-required automated matrix on 2026-10-07: Steel Closeout (`37641425299`), HPET (`37641425316`), SMP (`37641425398`), HDA (`37641425368`), NVMe (`37641425280`), USB Mass Storage (`37641425350`), USB Keyboard (`37641425387`) and USB Mouse (`37641425292`). PR #45 merged that candidate into the Road-to-15 integration branch as `8374b6ba66a8bb9a4d4ddf8f969816dc789a5be4`.

A later QEMU coexistence audit found two release-matrix holes: xHCI and
VirtIO-net shared a fixed virtual MMIO window, and xHCI still modelled only one
active USB HID slot/device context. Steel was correctly reopened rather than
preserving the earlier qualification claim.

The post-audit runtime candidate
`569bae4c33aa0c04db87f0cbe785da1aa45cc084` fixes both findings. VirtIO-net
now uses dedicated `0xFFFFB7...` virtual space with a host overlap regression,
while xHCI owns an independent companion HID context with its own slot, EP0 and
interrupt rings, DMA/report state, decoder and hotplug lifecycle. Foreign-slot
events are deferred across synchronous transactions instead of being consumed.

Same-SHA post-audit evidence:
- Steel Closeout `37661212373` — PASS;
- xHCI/VirtIO coexistence `37661212429` — PASS;
- simultaneous keyboard + mouse `37661212451` — PASS;
- full USB Keyboard regression `37661212449` — PASS;
- USB Mouse `37661212378` — PASS;
- USB Mass Storage `37661212361` — PASS;
- NVMe `37661212483` — PASS;
- Intel HDA `37661212532` — PASS;
- HPET `37661212464` — PASS;
- Steel Foundations `37661212233` — PASS.

The strengthened closeout itself now requires simultaneous USB keyboard + mouse
with VirtIO networking, real HID delivery, DHCP and gateway ICMP in addition to
the existing ACPI/HPET/four-vCPU SMP requirements.

The same audit was run against older source `2d550f1`; its observations that
USB Mass Storage, NVMe, SMP and Intel HDA were absent do not describe the current
integration line, where those implementations landed later and were requalified.
HID Boot Mouse wheel/report-protocol support remains a P2 limitation rather than
a claimed Steel capability.

Physical-machine/VirtualBox checks remain external validation and are not
silently converted into automated PASS markers.

## 5.1.0-dev — HAL / Driver Portability

Status: **IN PROGRESS**.

Turn the qualified 5.0 hardware substrate into a platform-neutral hardware
layer. QEMU, VirtualBox and VMware are qualification targets rather than kernel
architectures.

Required work:
- central boot-capability policy separating boot-critical infrastructure from
  optional hardware;
- PS/2 keyboard/mouse demoted to compatibility backends;
- USB HID and future transports feed the same generic input queue;
- driver failures isolated to their device/subsystem whenever continuing is
  memory/DMA/storage safe;
- no hypervisor-specific logic above concrete device backends;
- structured hardware inventory and degraded-mode diagnostics;
- USB-only boot qualification with i8042 disabled.

## 5.2.0-dev — Universal Input

- USB HID report-protocol parsing, not only boot protocol;
- relative mouse, absolute USB tablet and wheel support;
- multiple keyboards and pointers simultaneously;
- hotplug/unplug without losing the session;
- PS/2 fallback retained;
- groundwork for HID-over-I2C/touchpads without exposing transport details to
  Flux or applications.

## 5.3.0-dev — Universal Storage

- AHCI, NVMe and USB Mass Storage behind one block-device contract;
- controller-independent installer selection;
- removable-media lifecycle and safe detach;
- no storage backend assumed from hypervisor identity;
- qualification on SATA/AHCI, NVMe and USB media combinations.

## 5.4.0-dev — Platform / Firmware Portability

- UEFI/GOP baseline;
- ACPI/APIC/IOAPIC/HPET fallbacks and diagnostics;
- Intel and AMD SMP qualification;
- firmware quirks handled in bounded platform code rather than consumers;
- optional device absence never converted into a kernel-wide fatal condition.

## 5.5.0-dev — Hardware Compatibility Gate

- QEMU TCG/KVM;
- Oracle VirtualBox;
- VMware;
- physical Intel desktop/laptop;
- physical AMD desktop/laptop;
- Safe Mode and hardware inventory;
- compatibility-tier reporting and a public supported-hardware matrix.

5.5 closes only when the hardware layer is suitable for building the rest of
Road to 15 without emulator-specific assumptions.

## 6.0.0-dev — Kernel Core 2.0

PMM/VMM and independent address spaces, Scheduler 2.0, threads, processes/jobs,
per-CPU run queues, CPU affinity, preemption, IPC 2.0, shared memory,
synchronization primitives, VFS-facing process resource ownership and syscall
ABI qualification.

## 7.0.0-dev — Driver & Device Expansion

Broaden real hardware coverage while preserving the 5.x contracts:
- Intel E1000/E1000e;
- Realtek RTL8111/8168 class;
- VirtIO net/block;
- PCnet as legacy VM coverage;
- Intel HDA plus AC'97 fallback;
- USB hubs, hotplug and wider HID classes;
- device power-management foundation.

Adding a driver must not require changes in the network, storage, UI or
application layers that consume the generic interface.

## 8.0.0-dev — Flux Desktop Platform

Window Manager 2.0, compositor hardening, damage tracking, dynamic
resolutions, absolute/relative pointer support, scaling, clipboard, drag/drop,
desktop shell, dock/task switching, notifications, settings and stable GUI API.
UEFI GOP remains a valid software-rendered fallback; GPU acceleration is not a
requirement for desktop availability.

## 9.0.0-dev — Networking & Services

Network Service 2.0, hardened IPv4, IPv6, DHCP/DNS service, routing and
configuration, sockets, TLS 1.3, trust store/certificate validation, HTTPS API,
firewall foundation, reconnect/reconfiguration and clean operation when no
supported NIC exists.

## 10.0.0-dev — Storage & VFS 2.0

VFS 2.0, mount manager, production KuroFS evolution, FAT compatibility,
permissions, timestamps, symlinks, file locking, caching, journaling/recovery,
fsck, removable media and safe unmount. Filesystems consume generic block
devices and must not depend on AHCI/NVMe/USB controller details.

## 11.0.0-dev — Application Platform

Stable SDK/libc/libui, application manifests and lifecycle, package format,
permissions, launcher, file associations, process sandboxing, terminal, file
manager, settings, system utilities and the package-management client surface.

## 12.0.0-dev — Security, Updates & Recovery

Users/groups, permission enforcement, kernel/user validation, secure credential
storage, package/update signatures, atomic system updates, rollback, recovery
environment, repair mode, boot recovery, watchdog, crash dumps and production
Safe Mode.

## 13.0.0-dev — Performance & Power

Scheduler tuning, multicore scaling, memory pressure/page cache, async I/O,
interrupt balancing, MSI/MSI-X adoption, reduced polling, ACPI power lifecycle,
CPU idle/power states where supported, battery/thermal reporting and boot-time
optimization.

## 14.0.0-rc — Compatibility / Release Qualification

Feature freeze and product-level qualification:
- no core feature may depend on QEMU/VirtualBox/VMware identity;
- install/upgrade/reinstall/recovery matrices;
- storage interruption/corruption tests;
- USB hotplug and input churn;
- network disconnect/reconnect;
- long-running stress/uptime;
- multiple physical Intel and AMD systems;
- QEMU, VirtualBox and VMware regression;
- release-blocker list reduced to zero.

## 15.0.0 — STABLE

15.0.0 is the first product-level stable release. It must **BOOT, INSTALL, USE,
SECURE, UPDATE, RECOVER and DEVELOP FOR** on supported x86-64 UEFI hardware.

Minimum product contract:
- generic UEFI/GOP boot without hypervisor dependency;
- usable keyboard/pointer through supported USB HID or PS/2 fallback;
- persistent AHCI/SATA and NVMe storage, plus supported USB storage;
- SMP and stable kernel/userspace scheduling;
- framebuffer desktop with optional accelerated backends;
- networking and audio on documented supported devices;
- stable application/SDK/package interfaces;
- signed updates and recovery;
- a public hardware-support matrix and compatibility tiers.

No critical fake implementation and no claim of universal hardware support are
acceptable. Unsupported optional hardware must degrade cleanly rather than
taking down an otherwise usable system.

## Workflow

For every atomic work item:

`AUDIT -> DESIGN -> IMPLEMENT -> BUILD -> TEST -> FIX -> REGRESSION -> DOCUMENT -> COMMIT -> NEXT`

A real FAIL is fixed before dependent work continues. An unavailable external environment is documented but does not stop independent engineering.

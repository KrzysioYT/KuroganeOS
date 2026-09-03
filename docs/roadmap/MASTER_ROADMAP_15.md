# KuroganeOS Master Roadmap to 15.0.0

Status: ACTIVE
Baseline audited: `17bd55091c63544b9585840192f0eb288e9cffff`
Baseline version: `3.3.3-dev`
Final target: `15.0.0` (first official STABLE release)

## Mission

KuroganeOS remains a native x86-64 UEFI operating system with its own kernel, Ring-3 userspace, ELF64 applications, syscall ABI, drivers and system services. It must not be replaced by a Linux kernel/userspace, Wine-as-platform, or another operating system presented as KuroganeOS.

`15.0.0` is a release-quality gate, not a version-number goal and not a GUI redesign milestone.

## Priority order

1. SECURITY & RELIABILITY
2. HARDWARE & DRIVERS
3. CORE FUNCTIONALITY
4. PERFORMANCE
5. UI / VISUALS

Critical memory corruption, privilege escalation, kernel crashes, filesystem corruption, use-after-free, invalid user pointer handling, races, security vulnerabilities and lossy persistence block lower-priority work immediately.

## Truth policy

A feature can be marked `IMPLEMENTED`, `SUPPORTED` or `PASS` only when a real implementation, real backend and an appropriate positive test exist. Otherwise use `FOUNDATION`, `PARTIAL`, `EXPERIMENTAL`, `NOT QUALIFIED`, `PENDING`, or `UNSUPPORTED`.

## Release workflow

For every release:

`AUDIT -> DESIGN -> IMPLEMENT -> BUILD -> TEST -> FIX -> REGRESSION -> DOCUMENT -> COMMIT -> QUALIFY`

A release cannot close while a critical requirement is known broken. Hardware-only acceptance may remain pending while independent engineering continues, but the affected release gate must not be falsely marked PASS.

## Release sequence

### 3.3.x — Red Flux closeout

| Release | Primary gate |
|---|---|
| 3.3.3-dev | audited baseline: UEFI, kernel, Ring 3, ELF64, AHCI, FAT32/VFS, installer, networking, TCP/TLS work, AC'97, IPC/shared memory, Red Flux, QEMU/VirtualBox tooling |
| 3.3.4-dev | VirtualBox qualification: ISO UEFI boot, Try -> Login -> Desktop, SATA VDI install, reboot without ISO, installed-system boot, serial markers, regression fixes |
| 3.3.5-dev | installer reliability: persistence, GPT/ESP/root verification, EN/PL install, profile/password tests, destructive confirmation, recovery paths |
| 3.3.6-dev | network stabilization: E1000 RX/TX, async TX, packet loss, TCP retransmit/timeouts/cleanup/reset, DHCP/DNS, VirtualBox NAT |
| 3.3.7-dev | TLS foundation: stable TCP transport, partial/retryable BIO, entropy/RNG, cert infrastructure, real handshake and failure tests |
| 3.3.8-dev | userspace I/O: read/write/seek/stat/dirs, event/wait objects, userspace audio/network foundations, Ring-3 ownership/cleanup |
| 3.3.9-dev | feature freeze, cleanup, regressions, reproducible build, installer/network/persistence verification, 3.3 archive |

### 3.4.x — System services foundation

| Release | Primary gate |
|---|---|
| 3.4.0-dev | userspace services, registration/discovery, IPC broker |
| 3.4.1-dev | event broker, subscriptions, async/waitable events |
| 3.4.2-dev | Settings Service and typed persistent configuration |
| 3.4.3-dev | Notification Service and source/queue model |
| 3.4.4-dev | Account Service identity/profile ownership |
| 3.4.5-dev | Session Service, login/logout/home ownership/cleanup |
| 3.4.6-dev | filesystem userspace stat/readdir/create qualification |
| 3.4.7-dev | unlink/rename/mkdir/rmdir qualification |
| 3.4.8-dev | clipboard and desktop-neutral APIs; reduce Red Flux coupling |
| 3.4.9-dev | service crash/restart/resource cleanup qualification and archive |

### 3.5.x — Connected userspace

| Release | Primary gate |
|---|---|
| 3.5.0-dev | public socket ABI, ownership, permission foundation |
| 3.5.1-dev | async UDP and poll/events |
| 3.5.2-dev | async TCP connect/send/receive/close/errors |
| 3.5.3-dev | async DNS and timeout/result API |
| 3.5.4-dev | network status/link/address/gateway/DNS service |
| 3.5.5-dev | userspace TLS transport and async integration |
| 3.5.6-dev | HTTPS GET, certificate checks, response integration |
| 3.5.7-dev | async audio service, PCM streaming, ownership, stop/poll/events |
| 3.5.8-dev | application registry and manifest metadata foundation |
| 3.5.9-dev | connected-userspace qualification and archive |

### 3.6.x — Red Flux stabilization

| Release | Primary gate |
|---|---|
| 3.6.0-dev | compositor/render ownership cleanup |
| 3.6.1-dev | per-window surfaces |
| 3.6.2-dev | precise damage regions and redraw reduction |
| 3.6.3-dev | keyboard/pointer/focus cleanup |
| 3.6.4-dev | drag/resize/minimize/maximize/restore reliability |
| 3.6.5-dev | process/window ownership and close cleanup |
| 3.6.6-dev | app crash isolation; desktop survives app failure |
| 3.6.7-dev | boot/login/desktop/logout/relogin/session reliability |
| 3.6.8-dev | long-runtime, leak and exhaustion testing |
| 3.6.9-dev | final Red Flux qualification, documentation freeze, 3.x archive |

### 4.0.x — Pre-Steel architecture

| Release | Primary gate |
|---|---|
| 4.0.0-dev | Device Model 2.0 identity/tree/properties |
| 4.0.1-dev | Driver Manager 2.0 probe/bind/init/shutdown/failure |
| 4.0.2-dev | kernel/driver API boundaries |
| 4.0.3-dev | userspace/device handles and controlled access |
| 4.0.4-dev | structured error/status model |
| 4.0.5-dev | capability model foundation |
| 4.0.6-dev | deterministic process resource ownership |
| 4.0.7-dev | non-critical driver failure isolation |
| 4.0.8-dev | structured boot/driver diagnostics |
| 4.0.9-dev | architecture qualification, migration docs, Pre-Steel archive |

### 5.0.x — Steel foundation / hardware & drivers

| Release | Primary gate |
|---|---|
| 5.0.0-dev | Steel generation, Device Manager, driver lifecycle/state/errors |
| 5.0.1-dev | PCI/PCIe, BAR/MMIO/PIO, capabilities, MSI/MSI-X foundation |
| 5.0.2-dev | ACPI/MADT/APIC routing, HPET, power tables |
| 5.0.3-dev | SMP AP startup, per-CPU state, synchronization, scheduler groundwork |
| 5.0.4-dev | block layer and AHCI hardening/timeouts/error paths |
| 5.0.5-dev | real NVMe init/admin/identify/namespaces/I/O/read/write/error paths |
| 5.0.6-dev | xHCI core: discovery, command/event/transfer rings, enumeration/descriptors |
| 5.0.7-dev | USB HID keyboard/mouse, mass storage, disconnect/ownership |
| 5.0.8-dev | Intel HDA foundation, AC'97 compatibility, common audio/network driver model |
| 5.0.9-dev | driver/storage/USB/SMP stress and hardware compatibility database |

### 6.0.x — Core Steel

| Release | Primary gate |
|---|---|
| 6.0.0-dev | Kernel Core 2.0 architecture |
| 6.0.1-dev | PMM hardening and corruption diagnostics |
| 6.0.2-dev | VMM address spaces/mappings/page permissions |
| 6.0.3-dev | SMP-aware Scheduler 2.0 |
| 6.0.4-dev | threads/waiting/synchronization lifecycle |
| 6.0.5-dev | processes/jobs/parent-child/termination cleanup |
| 6.0.6-dev | IPC 2.0 channels, ownership, bounded queues |
| 6.0.7-dev | shared memory and mutex/semaphore/event primitives |
| 6.0.8-dev | VFS 2.0, mount lifecycle, permissions integration foundation |
| 6.0.9-dev | syscall ABI qualification, user/kernel validation, ABI regressions |

### 7.0.x — Iron Shield / security & reliability

| Release | Primary gate |
|---|---|
| 7.0.0-dev | formal threat model/trust boundaries/privileged component list |
| 7.0.1-dev | user pointer, syscall buffer and overflow validation |
| 7.0.2-dev | process/address-space isolation and executable/data permissions |
| 7.0.3-dev | users/groups and Kurogane identity model |
| 7.0.4-dev | filesystem/object permissions and ACL foundation |
| 7.0.5-dev | secure credential store and access restrictions |
| 7.0.6-dev | secure password KDF; remove FNV1A64-DEV; prefer Argon2id when feasible |
| 7.0.7-dev | app permissions and capability sandbox groundwork |
| 7.0.8-dev | watchdog, crash isolation/reports, service restart, panic diagnostics |
| 7.0.9-dev | syscall/input fuzzing, memory/process/fs stress, security qualification |

### 8.0.x — Connected Steel

| Release | Primary gate |
|---|---|
| 8.0.0-dev | Network Service 2.0 ownership |
| 8.0.1-dev | async socket subsystem |
| 8.0.2-dev | hardened IPv4/routing |
| 8.0.3-dev | IPv6 and ICMPv6 foundation |
| 8.0.4-dev | DHCP/DNS services and async resolver |
| 8.0.5-dev | persistent interface/route/network settings |
| 8.0.6-dev | TLS 1.3, secure entropy, sessions |
| 8.0.7-dev | CA store, chain/hostname/expiry validation and error reporting |
| 8.0.8-dev | HTTPS/secure socket APIs and firewall foundation |
| 8.0.9-dev | network/TLS/security/stress qualification |

### 9.0.x — Forge Graphics

| Release | Primary gate |
|---|---|
| 9.0.0-dev | Forge Graphics API architecture |
| 9.0.1-dev | graphics device abstraction and ownership |
| 9.0.2-dev | buffers/textures/resource memory/mappings |
| 9.0.3-dev | command recording/submission foundation |
| 9.0.4-dev | queues/fences/synchronization/barriers |
| 9.0.5-dev | swapchain/presentation/framebuffer integration |
| 9.0.6-dev | shader representation/compiler foundation/validation |
| 9.0.7-dev | real software 3D raster backend |
| 9.0.8-dev | real hardware-accelerated backend foundation; never fake acceleration |
| 9.0.9-dev | compositor integration, real telemetry/VRAM where available, stress qualification |

Direct3D 9/10/11/12 compatibility is allowed only as a real compatibility layer over working Forge Graphics. No fake device creation or feature-level success.

### 10.0.x — Steel applications

| Release | Primary gate |
|---|---|
| 10.0.0-dev | Application Runtime 2.0 |
| 10.0.1-dev | app lifecycle/manifests/permissions/launch/terminate/crash |
| 10.0.2-dev | Kurosh 2.0 shell, commands, scripting/pipelines foundation |
| 10.0.3-dev | Vault real filesystem manager operations |
| 10.0.4-dev | Performance real CPU/RAM/process/disk/network/graphics telemetry only |
| 10.0.5-dev | Forge Control settings/accounts/devices/network/display/audio |
| 10.0.6-dev | Pulse backend for network/audio/battery/power/performance/session |
| 10.0.7-dev | Kurogane Web shell, tabs/navigation/web-engine integration interface |
| 10.0.8-dev | app restart/isolation/session survival |
| 10.0.9-dev | core application stress/leak/cleanup qualification |

### 11.0.x — Anvil ecosystem

| Release | Primary gate |
|---|---|
| 11.0.0-dev | package file format |
| 11.0.1-dev | manifests/metadata/semantic versions |
| 11.0.2-dev | repository metadata/sync; official target `repo.kuroganeos.dev` |
| 11.0.3-dev | `anvil sync/search/info` |
| 11.0.4-dev | `anvil install/remove` |
| 11.0.5-dev | dependency resolver/conflicts/graph |
| 11.0.6-dev | SHA-256/signatures/verification/reject modified packages |
| 11.0.7-dev | transactional install and rollback groundwork |
| 11.0.8-dev | `anvil update/upgrade` and system update integration |
| 11.0.9-dev | SDK: `kurogane new/build/run/package`; compatible KuroganeOS-Packages evolution |

### 12.0.x — Kurogane platform / Web

| Release | Primary gate |
|---|---|
| 12.0.0-dev | Web Engine architecture/security/parser-render separation |
| 12.0.1-dev | HTTP/HTTPS navigation, redirects/failures |
| 12.0.2-dev | HTML parser and DOM |
| 12.0.3-dev | CSS parser/cascade/layout foundation |
| 12.0.4-dev | Flexbox |
| 12.0.5-dev | Grid, advanced text layout, overflow/clipping |
| 12.0.6-dev | image decoding and basic media groundwork |
| 12.0.7-dev | JavaScript runtime foundation without false full-compat claims |
| 12.0.8-dev | JS-DOM/events/cookies/local storage/history/tabs |
| 12.0.9-dev | dev console, web security/permissions/certificate UI/crash isolation qualification |

### 13.0.x — Forge design system

| Release | Primary gate |
|---|---|
| 13.0.0-dev | Forge design tokens |
| 13.0.1-dev | Obsidian/Forged Steel/Ash/Crimson/Hot Edge palette |
| 13.0.2-dev | typography and accessibility/readability |
| 13.0.3-dev | spacing/layout/sizing rules |
| 13.0.4-dev | blade/forged-panel geometry and edge/corner language |
| 13.0.5-dev | shared button/input/checkbox/toggle/slider/progress controls |
| 13.0.6-dev | lists/trees/tabs/menus/context menus/tooltips |
| 13.0.7-dev | windows/panels/dialogs/headers/navigation styling |
| 13.0.8-dev | icon family, motion/transitions/focus/hover/active/disabled states |
| 13.0.9-dev | core app migration to one reusable Forge design system |

### 14.0.x — Final Forge Desktop

| Release | Primary gate |
|---|---|
| 14.0.0-dev | final Forge Desktop shell/workspaces/compositor integration |
| 14.0.1-dev | Kurogane Spine |
| 14.0.2-dev | Blade Launcher with real manifests/search/categories |
| 14.0.3-dev | final Pulse panel using real backend data/actions only |
| 14.0.4-dev | final Vault + Forge Control |
| 14.0.5-dev | final Anvil/Kurosh/Kurogane Web integration |
| 14.0.6-dev | notifications/login/lock/power/session/accessibility/polish |
| 14.0.7-beta | FEATURE FREEZE: security/stability/bugfix/compat/performance only |
| 14.0.8-rc | full QEMU/VirtualBox/real-hardware/install/upgrade/recovery/stress qualification; unrun hardware stays PENDING |
| 14.0.9-rc | release blockers only; final media/SHA-256/docs/compatibility/SDK/package-repo preparation |

### 15.0.0 — First Official Steel Release

Channel: `STABLE`

15.0.0 may be set only after all critical gates are actually satisfied:

- UEFI boot -> kernel -> Login -> Forge Desktop;
- Try/Install/reboot/installed boot/persistence/update/recovery;
- memory/process isolation, syscall validation, users/groups/permissions, secure credentials/KDF, app permissions, crash/service recovery;
- SMP, PCI/PCIe, ACPI/APIC, AHCI, NVMe, xHCI, USB HID/storage, audio, networking;
- IPv4/IPv6/TCP/UDP/DHCP/DNS/TLS/certificate validation/HTTPS;
- Forge Graphics with stable compositor, resource ownership, synchronization and a real software or hardware backend;
- Kurosh, Vault, Performance, Forge Control, Pulse, Anvil and Kurogane Web;
- application runtime/manifests/permissions/SDK/package repository/signatures/dependencies/updater;
- filesystem integrity and memory/process/storage/network/graphics/long-runtime stress;
- QEMU, VirtualBox and at least one officially supported real-hardware class.

## Documentation lifecycle

Current documentation describes HEAD. Release documents under `docs/releases/` become historical after a release closes. Before generational breaking changes, snapshot the previous generation under `docs/archive/<major>.x/`. Breaking changes require migration documents covering ABI/syscalls, drivers, filesystem/config/account/package/app/graphics/update formats as applicable.

## Legacy lifecycle

`ACTIVE -> DEPRECATED -> LEGACY -> REMOVAL PLANNED -> REMOVED`

Removal requires rationale, target release, migration path, documentation and regression coverage.

## Steel / Forge identity

Steel / Forge generation begins at 5.0.0-dev. Final style begins in 13.x. Final look begins in 14.x.

**Built in Steel. Refined in Fire.**

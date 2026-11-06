# KuroganeOS Product Adoption Roadmap

Status: **ACTIVE STRATEGY — NOT A RELEASE CLAIM**
Target: **15.0.0 STABLE**
Companion to: [MASTER_ROADMAP_15.md](MASTER_ROADMAP_15.md)
Current implementation and qualification evidence: [DEVELOPMENT_STATE.md](../../DEVELOPMENT_STATE.md)

---

## 1. Product direction

KuroganeOS is an independent native operating system with its own x86-64 UEFI kernel, userspace, drivers, services, application interfaces and Flux desktop. It is **not a Linux distribution**, and this roadmap does not propose replacing its kernel with Linux.

The long-term goal is to make KuroganeOS a credible desktop alternative that technically curious Linux users can evaluate, install, use for real work, develop for and recommend — while keeping its own architecture and visual identity.

The product promise for 15.0.0 is:

> A user can install KuroganeOS on documented supported hardware, complete ordinary desktop tasks, install trusted applications, connect to the network, preserve their data, receive and recover from updates, and build software for the platform without relying on undocumented workarounds.

A good-looking desktop is necessary but not sufficient. Adoption depends on the complete experience: installation, hardware, applications, compatibility, security, documentation, support and community trust.

## 2. Product principles

1. **Keep the native OS.** Do not turn KuroganeOS into a Linux distribution, a Windows shell or a collection of emulated screenshots.
2. **Build for real tasks.** Every milestone must name what a user or developer can newly accomplish.
3. **Prefer a narrow, proven hardware list over false universal compatibility.** Unsupported optional devices must fail safely and explain their status.
4. **Do not confuse API compatibility with binary compatibility.** POSIX ports, native Kurogane applications and Linux binary execution are separate capabilities.
5. **Treat data safety and security as release requirements.** A feature is not complete if it can silently corrupt user data or bypass account ownership.
6. **Keep fallbacks.** GOP software rendering, Classic desktop and supported legacy input paths remain usable while newer backends mature.
7. **Publish evidence.** Every compatibility claim must name the exact source SHA, build/media hash, device or VM profile, test method and result.
8. **Do not promise an ecosystem before it exists.** A package manager, browser, Wi-Fi support or Linux compatibility must be demonstrated, not inferred from a menu entry or API stub.

## 3. Adoption readiness levels

These levels describe product readiness independently of the formal version number.

| Level | Meaning | Minimum evidence |
|---|---|---|
| **Engineering build** | For developers working on the OS | Reproducible build, host tests, serial diagnostics and documented limitations |
| **Developer preview** | For OS enthusiasts and application authors | Boots on a published VM profile, authenticated session, usable Terminal/Files, SDK sample builds and runs |
| **Public desktop preview** | For testers who accept known limitations | Safe installer, working desktop journey, supported network/audio profile, clear hardware matrix, issue reporting and recovery instructions |
| **Daily-driver beta** | For users trying it as a primary desktop on selected machines | Stable storage, practical applications, supported Wi-Fi, updates/rollback, account security, physical-device validation and sustained-use testing |
| **15.0.0 Stable** | First product-level stable release | All mandatory release gates in Sections 8–10 pass on the exact release candidate SHA |

A successful QEMU boot does not promote the project to a public desktop preview. A headless VirtualBox test does not prove manual usability. A feature marked implemented is not automatically qualified.

## 4. The complete user journeys

The product is not ready for migration until these journeys work end to end on documented supported hardware.

### 4.1 First contact and installation

- Download a clearly versioned ISO and verify its published checksum and signature.
- Boot a Try/live environment without modifying internal storage.
- Identify keyboard, pointer, display, network and audio capabilities.
- Start the installer, understand disk choices and see an explicit destructive-action warning.
- Install to a selected disk or partition without silently touching unrelated disks.
- Reboot without the installation media and reach the installed desktop.
- Recover from a failed or interrupted installation using documented recovery media.

**Acceptance:** fresh-install, existing-data safety, cancellation, disk-only reboot and recovery tests pass on every release-candidate storage profile.

### 4.2 Daily desktop work

- Sign in and reach a responsive desktop.
- Open, move, resize, minimize, restore and close applications.
- Find and launch applications using mouse and keyboard.
- Browse, create, copy, rename and move files on persistent storage.
- Use Terminal for normal administrative and development tasks.
- Copy and paste between supported applications.
- Change appearance and accessibility settings and keep them after restart.
- Understand an application failure and recover without losing the session.

**Acceptance:** complete task scripts are executed with real user input and real application processes. Screenshots alone do not count.

### 4.3 Connected desktop

- Configure a supported Ethernet or Wi-Fi adapter.
- Connect to a secured network and reconnect after link loss or reboot.
- Resolve DNS, validate HTTPS certificates and show useful connection state.
- Play audio through a documented supported device.
- See a clear explanation when Wi-Fi, Bluetooth, microphone or another optional device is unsupported.

**Acceptance:** network and audio tests cover fresh boot, reconnect, suspend/resume where supported and failure recovery.

### 4.4 Applications and updates

- Discover an application in a trusted catalogue.
- Inspect its publisher, version, permissions and dependencies.
- Install, launch, update and remove it.
- Verify packages before installation and reject invalid signatures.
- Update the OS without losing user data.
- Roll back a failed system update through a documented recovery path.

**Acceptance:** package and OS update tests include tampering, interruption, low disk space, failed boot and rollback.

### 4.5 Developer onboarding

- Install the supported SDK/toolchain on a documented host.
- Build a minimal native GUI application from public headers.
- Run it in KuroganeOS and receive keyboard/pointer events.
- Package it and install it through the supported package workflow.
- Read diagnostics and understand ABI/API compatibility guarantees.

**Acceptance:** a developer who did not write the kernel can follow the quickstart from a clean host environment.

## 5. Cross-cutting workstreams

### 5.1 Licensing, contribution and community trust — P0 decision

The current repository uses KSAL-2.0, a source-available licence, not an OSI-approved Open Source licence. This is a strategic adoption constraint, not a code defect.

Before actively recruiting external contributors or promoting KuroganeOS as an open community platform, the project owner must explicitly choose and document one of these directions:

- **Open-source community model:** select an established OSI-approved licence compatible with the project's goals and third-party components; review the CLA and contribution process accordingly.
- **Source-available independent project:** retain the current model, explain commercial, redistribution and derivative-work restrictions prominently, and accept that some developers, packagers and organizations will not participate.

Recommended direction for broad Linux-community adoption: evaluate an OSI-approved licence for the project code, with a separate trademark policy and clear third-party notices. This is a recommendation, **not authorization to change the licence**. Preserve valid historical MIT grants and obtain qualified legal review before a licence migration.

Required supporting work:
- clear contribution guide and maintainer responsibilities;
- issue triage and security-reporting process;
- public roadmap and release notes;
- documented decision process for API/ABI changes;
- explicit third-party code, font, firmware and asset provenance;
- a contributor agreement proportionate to the selected licence.

**Exit gate:** users can determine what they may study, modify, build, fork, redistribute and contribute without having to infer it from legal text.

### 5.2 Application compatibility strategy — P0/P1

Maintain three distinct application paths:

| Path | Purpose | Priority |
|---|---|---|
| **Native Kurogane SDK** | Best integration and long-term ABI for platform applications | P0 |
| **KuroPOSIX** | Port source applications with familiar POSIX interfaces | P1 |
| **Linux binary compatibility** | Run selected existing Linux ELF binaries without rebuilding | P2 research track; do not promise for 15.0 unless proven |

KuroPOSIX should start with the interfaces required by real ports: file descriptors, file I/O and metadata, process creation/wait, pipes, polling, sockets, memory mapping, signals where safe, and a practical pthread subset. Compatibility must be tested with real ported applications, not only header presence.

Do not claim complete POSIX merely because common functions compile. Publish a conformance matrix and document unsupported semantics.

Linux binary compatibility is a separate, much larger undertaking involving Linux syscall behavior, ELF loading, dynamic linking, threading, signals and library/runtime expectations. Do not let it delay a useful native platform or a measured POSIX porting layer.

### 5.3 Hardware and driver strategy — P0

Build a public compatibility matrix with three user-facing tiers:

- **Tier A — Certified:** installed and exercised on real hardware with working display, input, storage, networking, audio, updates and recovery.
- **Tier B — Supported:** known device IDs and drivers, with automated and/or targeted device tests; any unverified features are listed.
- **Tier C — Basic boot:** system reaches a safe desktop or recovery mode, but one or more major functions may be unavailable.

Initial physical validation should deliberately cover more than one machine family:
- Intel desktop or laptop with integrated graphics;
- AMD desktop or laptop with integrated graphics;
- one system with a supported discrete GPU;
- at least one machine with common USB, NVMe/SATA and Ethernet hardware;
- one laptop with a supported Wi-Fi adapter, touchpad, audio and battery reporting.

Prioritize common device families, not brand names alone:
- Intel and AMD graphics, beginning with a small documented set;
- Intel and Realtek Wi-Fi/Ethernet where chipset documentation and legal terms permit;
- common USB HID devices, hubs and storage;
- HID-over-I2C touchpads;
- Intel HDA codecs and common audio paths;
- Bluetooth controllers;
- NVMe, AHCI/SATA and USB storage.

Do not imply that a laptop is supported because its CPU or motherboard is recognized. A support entry must list the tested display, networking, audio, input and storage functions separately.

### 5.4 Graphics and desktop platform — P0/P1

- Keep UEFI GOP/software rendering as a reliable fallback.
- Complete native compositor lifecycle, focus, capture, multi-window interaction, clipping, damage tracking and accessibility.
- Complete licensed scalable typography, Unicode coverage, shaping/line layout and reliable HiDPI.
- Add a real accelerated graphics backend for a small, documented set of GPUs before claiming hardware acceleration.
- Design graphics APIs around explicit capabilities; return unsupported errors instead of pretending to implement Direct3D, Vulkan, OpenGL or Wayland compatibility.
- Measure frame pacing, input latency, memory and rendering performance on physical hardware.

Hardware acceleration should be a product goal, but universal NVIDIA/AMD/Intel support is not a prerequisite for the first working preview. At least one physically validated accelerated path should be part of the daily-driver beta plan.

### 5.5 Networking and peripherals — P0/P1

- Finish robust Ethernet lifecycle, DHCP/DNS, TCP/TLS and reconnect behavior.
- Add Wi-Fi management with secure authentication and clear saved-network ownership.
- Add IPv6 and dual-stack diagnostics.
- Add Bluetooth discovery, pairing and reconnect for documented devices.
- Provide stable device status and logs in Settings.
- Implement safe hotplug/unplug for supported USB devices.
- Add suspend/resume only with tests for active storage, network, audio and input recovery.

### 5.6 Storage and data durability — P0

- Finish a persistent writable filesystem and a stable VFS contract.
- Add file ownership, permissions, timestamps, links and robust descriptor/handle lifetime.
- Implement flush, safe unmount, journaling or an equivalent crash-consistency design, filesystem checking and recovery.
- Test power loss and forced reset during writes, metadata updates, package installation and OS updates.
- Protect user data during installer operations and upgrades.
- Publish filesystem limits and recovery behavior.

Do not call storage production-ready because a file survives a normal reboot. It must also survive interrupted operations and report corruption safely.

### 5.7 Security, updates and recovery — P0

- Strong password derivation and secure credential storage; remove development-only hashing.
- Enforced users, groups, permissions and process ownership.
- A clear privilege boundary and documented application capabilities.
- Process isolation and resource limits; sandboxing appropriate to the application model.
- Signed packages and system updates with key-rotation/revocation policy.
- Atomic update activation and automatic/manual rollback.
- Recovery environment, safe mode, boot repair and crash diagnostics.
- Disk encryption for supported installations, with documented recovery-key handling.
- Security advisories and a process for handling vulnerabilities.

A security feature is not complete until negative tests show that an unprivileged app cannot access another user's data, bypass authentication or install untrusted privileged code.

### 5.8 Application ecosystem — P0/P1

Before asking users to migrate, provide a useful baseline of maintained applications:

| Category | Minimum product outcome |
|---|---|
| Web | A secure, maintainable browser with HTTPS, downloads and modern web support appropriate to the platform |
| Files | Persistent storage browser, copy/move/rename, removable media and clear file errors |
| Terminal | Interactive shell, history, completion, pipes and scripting foundation |
| Settings | Accounts, display, network, audio, Bluetooth, privacy, updates and devices |
| Text and documents | Native text editor and a documented path to office/document formats |
| Media | Audio playback, volume control and common media formats where licensed |
| Development | SDK, compiler/toolchain, editor integration, debugger/diagnostics and Git workflow |
| System tools | Archive manager, screenshot capture, task/process monitor and recovery utility |

Do not build every application internally. A stable SDK, POSIX porting layer, package format and good documentation should let other developers contribute applications.

### 5.9 Developer experience and governance — P1

- Stable public SDK headers and versioned API/ABI policy.
- Native GUI and command-line application templates.
- Toolchain bootstrap for Windows/WSL, Linux and macOS hosts.
- Reproducible builds, package creation and local testing.
- Useful crash traces, logs and debugging tools.
- Clear contribution instructions, review expectations and security disclosure.
- A small, published list of supported development workflows.

## 6. Formal milestone outcomes

The formal version sequence remains owned by [MASTER_ROADMAP_15.md](MASTER_ROADMAP_15.md). The following is the product-facing interpretation of those milestones.

### 7.0.0-dev — Iron Shield / hardware foundation

**Outcome:** expand supported device families behind generic driver contracts.

Exit criteria:
- supported-device profile passes on an exact SHA;
- USB keyboard and pointer, storage, networking and SMP coexist without device-specific assumptions in higher layers;
- optional device failure is isolated and diagnosable;
- hardware limitations are published.

This is a hardware engineering milestone, not a user-facing stable release.

### 8.0.0-dev — Flux Modern / usable desktop

**Outcome:** a user can sign in, personalize the desktop, launch and use real applications, manage files, operate Terminal, understand errors and recover from ordinary application failures.

Exit criteria:
- authenticated Login → Home and real Ring-3 app launch;
- usable Files, Terminal, Settings and browser path;
- keyboard-first navigation, visible focus and accessible feedback;
- licensed scalable fonts, Polish text and tested HiDPI layouts;
- clipboard and app lifecycle work across the desktop;
- appearance settings persist safely;
- native SDK GUI example builds and runs;
- QEMU and VirtualBox exact-SHA tests, including visible manual interaction, pass.

8.0 does **not** promise a complete package ecosystem, Linux binary compatibility or universal hardware acceleration.

### 9.0.0-dev — Networking and connectivity

**Outcome:** reliable wired and supported wireless networking, secure web connectivity, clear offline behavior and reconnect after common failures.

Exit criteria:
- production network service and documented NIC profiles;
- secure Wi-Fi workflow for selected adapters;
- IPv6/dual-stack baseline;
- DNS, TLS certificate validation and HTTPS regression;
- reconnect and device-removal recovery;
- clear network diagnostics in Settings.

### 10.0.0-dev — Storage and VFS 2.0

**Outcome:** persistent files and applications survive normal use, restarts and tested interruptions.

Exit criteria:
- writable persistent filesystem with stable VFS/descriptor contracts;
- permissions, timestamps, links and safe removable-media behavior;
- crash-consistency strategy, fsck/repair and recovery;
- power-failure and interrupted-write tests;
- safe installer, migration and filesystem upgrade procedures.

### 11.0.0-dev — Application platform

**Outcome:** third-party developers can build, package, distribute and maintain native applications.

Exit criteria:
- stable SDK/libc/libui contracts and documented ABI policy;
- package format, repository metadata, dependency resolution and signature verification;
- install/update/remove/rollback workflows;
- application permissions and file associations;
- KuroPOSIX porting subset validated with real applications;
- a useful baseline app catalogue and public developer quickstart.

Linux binary compatibility remains a separately gated research effort unless its scope and feasibility are demonstrated.

### 12.0.0-dev — Security, updates and recovery

**Outcome:** users can trust account boundaries, install verified software, update safely and recover from a failed update or boot.

Exit criteria:
- production credential KDF and secure secret handling;
- enforced users/groups/permissions and process isolation;
- signed package and OS update chain;
- atomic update and rollback tested under interruption;
- recovery environment, safe mode and boot repair;
- security review with no unresolved critical/high release blockers;
- documented security response process.

### 13.0.0-dev — Performance and power

**Outcome:** the system feels responsive and behaves reasonably on supported desktops and laptops.

Exit criteria:
- measured boot time, memory, desktop frame pacing and input latency;
- scheduler and I/O performance baselines with regression limits;
- suspend/resume and idle-power behavior on documented machines;
- battery and thermal telemetry where supported;
- no misleading GPU/CPU/disk utilization indicators;
- long-duration and resource-pressure testing.

### 14.0.0-rc — Product release candidate

**Outcome:** freeze features and validate the whole product, not isolated subsystems.

Exit criteria:
- multiple physical Intel and AMD systems across documented tiers;
- clean install, upgrade, reinstall, data-preservation and recovery matrix;
- Wi-Fi, Ethernet, Bluetooth, audio, USB, storage and display validation;
- application install/update/remove and rollback;
- security review and vulnerability triage;
- long-running workload, resource pressure, crash and power-interruption tests;
- published known-issues list and support matrix;
- zero unresolved release-blocking data-loss, security, boot or session-ownership defects.

### 15.0.0 — first stable release

KuroganeOS 15.0.0 may be called stable only when it can:

- **BOOT** on supported UEFI hardware without hypervisor dependency;
- **INSTALL** safely and recover from failed installation;
- **USE** a complete desktop for ordinary daily tasks;
- **CONNECT** using documented Ethernet/Wi-Fi profiles and supported peripherals;
- **STORE** user data persistently and recover from tested interruptions;
- **RUN APPS** from a trusted native package ecosystem;
- **DEVELOP FOR** through a documented, stable SDK;
- **SECURE** accounts, applications, packages and updates;
- **UPDATE** with signature verification and rollback;
- **RECOVER** from failed boot, update and filesystem problems.

No claim of universal PC support is required. A truthful, tested compatibility matrix is required.

## 7. Priority policy

### P0 — required for a credible desktop product

- Complete Flux desktop and reliable first-run journey.
- Installer safety, persistent storage and recovery.
- Clear licence/community model decision.
- Basic supported network connectivity, including at least a documented Wi-Fi path before daily-driver beta.
- Usable native SDK and essential built-in applications.
- Account boundaries, secure credentials and signed update foundation before stable.
- Public hardware matrix and repeatable physical-device testing.

### P1 — required for broad adoption

- KuroPOSIX and real source ports.
- A validated accelerated graphics path.
- Bluetooth, broad audio, touchpad and hotplug support.
- Package repository, dependency management and application catalogue.
- Developer tooling, debugging and API stability.
- Accessibility, localization and polished settings.

### P2 — valuable expansion; not a reason to fake early support

- Linux binary compatibility.
- Broad compatibility with every GPU vendor and model.
- Gaming compatibility layers such as Wine/Proton or Windows DirectX execution.
- Full Wayland protocol compatibility.
- Complete Microsoft Direct3D API compatibility.
- Exotic peripherals and highly specialized workstation/server use cases.

P2 work may be researched earlier, but it must not displace P0 product safety or be advertised as available without evidence.

## 8. Release acceptance scorecard

Each feature is tracked through these states:

PLANNED → IMPLEMENTED → HOST-TESTED → GUEST-VERIFIED → PHYSICAL-VERIFIED (where applicable) → RELEASE-QUALIFIED

For every release candidate, maintain a scorecard covering:

| Area | Required evidence |
|---|---|
| Boot and installer | Fresh boot, Try, safe install, cancellation, disk-only boot, repair/recovery |
| Desktop | Real input, real processes, multi-window tasks, accessibility and failure recovery |
| Hardware | Exact models, device IDs, functions tested and known limitations |
| Network | Ethernet/Wi-Fi, DNS, TLS, reconnect, offline and recovery |
| Storage | Persistence, corruption handling, interruption and recovery |
| Applications | Package verification, install/update/remove and real app workflows |
| Security | Negative authorization tests, credential handling, signatures and rollback |
| Performance | Reproducible baselines and long-duration/resource-pressure results |
| Developer platform | Clean-host SDK build, native sample, package and debugging workflow |
| Documentation | Installation guide, known issues, compatibility matrix, release notes and recovery guide |

Every PASS must name the exact source SHA, job/log, media hash, test profile and scope. A newer commit invalidates only the affected evidence gates; unrelated evidence must not be silently transferred to a different SHA.

## 9. Product metrics

Collect measurements before setting numerical targets. Do not invent adoption or performance statistics.

Track:
- install success and failure reasons by hardware profile;
- first-boot and first-login success;
- desktop task completion and application crash rate;
- device support by exact model and function;
- network reconnect and suspend/resume success;
- filesystem recovery and data-integrity test results;
- package install/update/rollback success;
- SDK quickstart completion by an independent developer;
- open critical/high issues and time to resolution;
- measured boot time, memory footprint, frame pacing and input latency.

Telemetry must be opt-in, privacy-conscious and documented. Do not collect personal data merely to improve a dashboard.

## 10. Explicit non-goals and honest claims

Until separately implemented and qualified, do not claim:
- that KuroganeOS is a Linux distribution or runs ordinary Linux applications;
- complete POSIX or Linux ABI compatibility;
- universal Wi-Fi, Bluetooth, GPU or laptop support;
- hardware 3D acceleration when the active path is GOP/software rendering;
- Wayland protocol, full Direct3D or Windows application compatibility;
- production-grade encryption, signed updates or rollback before their gates pass;
- stable release status based only on a successful ISO build or VM boot.

The product may be ambitious without overstating what currently works.

## 11. Decision log

The following decisions require explicit project-owner approval; this roadmap does not silently make them:

1. Licence strategy and whether to move from KSAL-2.0 to an OSI-approved licence.
2. Exact initial physical hardware certification list.
3. First supported Wi-Fi and accelerated-GPU families.
4. Which POSIX interfaces are guaranteed in the initial KuroPOSIX profile.
5. Package repository signing-key and update trust model.
6. Whether Linux binary compatibility is a funded 15.x goal or a later research track.

## 12. Relationship to implementation documents

- [Master roadmap and formal versions](MASTER_ROADMAP_15.md)
- [Flux Modern 8 completion contract](FLUX8_MODERN_COMPLETION.md)
- [Rebuild and engineering workstreams](KUROGANEOS_REBUILD_7_TO_15.md)
- [Current implementation evidence](../../DEVELOPMENT_STATE.md)
- [Current limitations](../CURRENT_LIMITATIONS.md)
- [Hardware compatibility matrix](../compatibility/HARDWARE_MATRIX.md)
- [Licensing policy](../LICENSING.md)
- [Developer documentation](../DEVELOPERS/README.md)

# KuroganeOS Road to 15 status

This file is generated only from qualified development evidence.

## 3.6 Flux Stabilization

Status: **QUALIFIED** at source SHA `0caf8cc42f872b11b44f874029eb41aeae152abc`.

Authoritative evidence:
- Flux Runtime Core run `33530401377` — PASS;
- Flux Session Recovery run `33530403709` — PASS;
- 3.4 regression sweep run `33530406070` — PASS;
- 3.5 Connected Userspace closeout run `33530408164` — PASS;
- same-SHA Flux Stabilization closeout run `33530392489` — PASS.

## 4.0 Pre-Steel / KuroFS 1.0

Status: **QUALIFIED** at source SHA `bbec12248773930d6f40aa98837ff13e99b7cf5e`.

Active work is built on persistent KuroFS allocation primitives over the production `storage::block::Device` contract. The current engineering slice includes revision-checked regular-file growth, zero-filled expansion, truncate-to-zero, contiguous in-place extension, publication-atomic copy-on-write data writes and post-publication extent reclamation. An ambiguous inode-publication failure preserves both old and replacement allocations, allowing remount to expose one complete payload without freeing a possibly live extent. Truncate interruption similarly exposes either the complete old file or a valid empty inode. Ambiguous create publication preserves the pending child so mount can classify it from the durable parent record instead of tombstoning a possibly live inode. Directory copy-on-grow returns its superseded extent to the allocator. Copy-on-write unlink compacts the parent, refuses non-empty directories, retires the detached inode with an advanced generation and only then reclaims data. Same-directory rename uses copy-on-write, while cross-directory file and non-empty-directory moves prepare both replacement images before recording a redundant-superblock move intent. Remount recovery either retains the old namespace or completes a destination-first publication before exposing the filesystem. Mount refuses invalid live inode metadata, references to free extents, overlapping live extents, stale directory identities and duplicate child ownership. Inode slots now persist explicit pending and orphan ownership, distinguish free slots from generation-carrying tombstones and normalize interrupted namespace attachment during mount. Explicit bounded reclamation tombstones regular-file and empty-directory orphans before releasing their extents, while non-empty orphan trees and ambiguous raw block reservations remain deferred and untouched. Deterministic write/flush interruption, remount, sparse-write, stale-writer, no-space, ownership-transition, create, orphan-reclaim, tombstone-reuse and reclaimed-range host tests pass.

Native KuroFS runtime persistence was qualified at exact source SHA `6dd9581e79d79bcd5155b4aa719d7ffcf1a1f8b1` by Actions run `33817447611`. The complete Pre-Steel candidate then passed same-SHA Actions closeout run `34260827773`; final authoritative job `102186252372` recorded `KuroganeOS 4.0 Pre-Steel closeout: PASS sha=bbec12248773930d6f40aa98837ff13e99b7cf5e`.

## 5.0 Steel / Hardware

Status: **QUALIFIED AFTER COEXISTENCE AUDIT**.

Authoritative post-audit runtime candidate:
`569bae4c33aa0c04db87f0cbe785da1aa45cc084`.

The first 2026-10-07 closeout matrix was insufficient: an external QEMU audit
against older source reproduced an xHCI/VirtIO MMIO collision and the
single-active-HID limitation. The current post-audit candidate fixes and
qualifies those configurations rather than discarding the audit.

Post-audit exact-candidate evidence:
- Steel Closeout `37661212373` — PASS;
- xHCI + VirtIO coexistence `37661212429` — PASS;
- simultaneous xHCI keyboard + mouse `37661212451` — PASS;
- xHCI USB Keyboard ring-wrap/hotplug/late-attach `37661212449` — PASS;
- xHCI USB Mouse `37661212378` — PASS;
- xHCI USB Mass Storage `37661212361` — PASS;
- NVMe Block Runtime `37661212483` — PASS;
- Intel HDA Runtime `37661212532` — PASS;
- HPET Main Counter `37661212464` — PASS;
- Steel Foundations `37661212233` — PASS;
- CLA `37661212400` — PASS.

The strengthened closeout requires a clean release build, ACPI power, HPET,
four-vCPU AP startup/cross-CPU work/TLB shootdown and a combined runtime with
USB keyboard + USB mouse + VirtIO networking, real HID input, DHCP and gateway
ICMP. The dedicated coexistence workflow additionally qualifies both individual
HID devices with VirtIO MSI-X and a VirtIO polling fallback.

Implementation closure:
- VirtIO-net no longer aliases the xHCI `0xFFFFB2...` region; it uses a
  dedicated `0xFFFFB7...` mapping region. Host regression also protects the
  B3 APIC space from accidental reuse.
- xHCI has an independent companion HID slot/context with separate EP0 and
  interrupt rings, DMA/report state, decoder and hotplug lifecycle.
- unrelated slot events are preserved across synchronous xHCI control/BOT
  operations instead of being discarded.
- simultaneous USB keyboard + mouse is proven by real QEMU input delivery.

The audit's statements that USB Mass Storage, NVMe, SMP and Intel HDA were
absent were correct for its older `2d550f1` source snapshot, not for the
current integration line. Those implementations remain present and passed the
post-audit regression.

The SMP scope remains the multi-CPU execution substrate, not Scheduler 2.0.
Full process/thread multicore scheduling is 6.0 Core Steel.

HID Boot Mouse wheel/report-protocol support remains an explicit P2 limitation
and is not falsely reported as qualified. Oracle VirtualBox and physical
hardware remain external validation.

## 5.1 HAL / Driver Portability

Status: **IN PROGRESS — NOT YET QUALIFIED**.

The project direction changed after real VirtualBox testing exposed the cost of
treating one VM profile as the hardware contract. KuroganeOS now targets
generic x86-64 UEFI systems; hypervisors are qualification targets only.

Initial 5.1 implementation:
- central `hardware::policy` capability evaluation;
- Device Model `Optional` / `BootCritical` requirements with Driver Manager
  isolation of optional bind failures;
- structured boot-time hardware inventory including bus, type, PCI IDs,
  requirement, status and owning driver;
- only timer, timer-scheduling hook and generic input queue are boot-critical
  in the first portability slice;
- PS/2 keyboard/mouse are optional compatibility backends rather than boot
  requirements;
- xHCI USB HID continues to feed the same generic input queue;
- network driver/DHCP/gateway failure degrades networking instead of halting
  an otherwise usable system;
- QEMU smoke tooling gained an `--no-ps2` mode using an i8042-disabled q35
  machine;
- a dedicated portability gate requires Red Flux plus real USB keyboard and
  mouse input with PS/2 absent.

This section records active engineering only. It must not be treated as a
qualified 5.1 milestone until the dedicated workflow and required regression
matrix are green on an exact candidate.

## 5.2 Universal Input

Status: **IMPLEMENTED CANDIDATE — AWAITING EXACT-SHA QUALIFICATION**.

The generic input contract now supports relative and absolute pointers.
USB HID report descriptors are parsed for pointer X/Y, logical ranges, buttons,
wheel and optional report IDs. xHCI can fetch a HID report descriptor, register
a report-protocol pointer and route relative reports through the shared mouse
contract or absolute reports through framebuffer-scaled
`input::submit_absolute_pointer`.

The dedicated 5.2 runtime gate disables i8042 and combines a real xHCI USB
keyboard with QEMU's USB tablet. It requires real keyboard delivery, absolute
pointer delivery, multi-HID enumeration and Red Flux startup. Boot-protocol
keyboard/mouse remain supported fallbacks.

## 5.3 Universal Storage

Status: **IMPLEMENTED CANDIDATE — AWAITING EXACT-SHA QUALIFICATION**.

`storage::device_registry` is now controller-independent. AHCI, NVMe and
xHCI USB Mass Storage publish the same `storage::block::Device` contract into
a central fixed-size registry. USB hot-unplug unregisters the device before
child teardown.

The installer consumes generic input events and enumerates the generic block
registry instead of requiring PS/2 plus AHCI. GPT/root-volume probing, scratch
qualification and auxiliary KuroFS probing now consume registry entries rather
than assuming an AHCI provider. The 5.3 gate combines NVMe and USB Mass Storage
and requires both to remain visible through the unified runtime.

## 5.4 Platform / Firmware Portability

Status: **IMPLEMENTED CANDIDATE — AWAITING EXACT-SHA QUALIFICATION**.

A neutral x86 CPU discovery layer reports CPUID vendor, family/model/stepping
and baseline TSC/MSR/APIC/x2APIC/SSE2/NX/long-mode capabilities. The kernel
does not select a different scheduler or SMP architecture by vendor.

The 5.4 gate boots the same UEFI/ACPI/APIC/SMP code with Intel-like
`GenuineIntel` and AMD-like `AuthenticAMD` CPUID identities, four vCPUs,
HPET, AP startup, cross-CPU work and TLB shootdown.

## 5.5 Hardware Compatibility Gate

Status: **IMPLEMENTED CANDIDATE — AWAITING EXACT-SHA QUALIFICATION**.

Transport-neutral capabilities now distinguish display, keyboard, pointer,
storage, network, audio, USB host and multiprocessor support. Runtime reports
one of:

- Tier 0 Boot;
- Tier 1 Usable;
- Tier 2 Connected;
- Tier 3 Extended.

The automated 5.5 gate requires both a PS/2-free Tier 1 profile
(USB keyboard + report-protocol tablet + NVMe) and a Tier 3 profile
(4 vCPU + USB + NVMe + E1000 networking + Intel HDA).

Oracle VirtualBox, VMware and physical Intel/AMD machines remain external
validation, not fabricated automated evidence. They do not block independent
Kernel Core 2.0 engineering, but any reproduced real-hardware failure reopens
the affected 5.x portability subsystem. Broad physical qualification remains
mandatory before the final release-candidate/stable gate.


## 6.0 Core Steel / Kernel Core 2.0

Status: **QUALIFIED** at exact source SHA
`717e003b34d47c81495e8ac5c580ae57c5fb16b3`.

Authoritative evidence:
- Scheduler 2.0 Foundation run `37710497664` — PASS;
- Core Steel Closeout run `37710493815` — PASS.

The closeout passed the full host suite, kernel/thread/process/IPC regressions,
a clean release-media build and four-vCPU QEMU runtime. Runtime evidence
includes Scheduler 2.0 topology, kernel and Ring-3 preemption, AP startup,
Scheduler 2.0 execution across CPUs, Ring-3 execution on APs, cross-CPU work,
TLB shootdown, DHCP/gateway networking, Red Flux login -> desktop and a real
mouse launch of Kurogane Web. VirtualBox/VMware/physical-machine evidence
remains external.

## 7.0 Iron Shield / Driver & Device Expansion

Status: **ACTIVE CANDIDATE — NOT YET QUALIFIED**.

Initial implementation expands the existing Intel GbE driver from 82540EM
(`8086:100E`) to the E1000e-class 82574L (`8086:10D3`) while retaining the
same generic NetworkInterface. A dedicated exact-SHA gate now requires host
classification regression plus clean-media QEMU DHCP/gateway runtime on both
`e1000` and `e1000e`. RTL8111/8168, wider USB/hub support and device power
management remain open 7.0 scope.

## 2026-10-20 consolidated 7.0 candidate

Branch: `codex/road-to-15-stability`. Integrates `11f426c` (timed/recoverable
PCI PM) and `5ebb515` (modern VirtIO block and shared PCI transport).
The newer Intel GbE classifier and PCI PM implementation are retained.

The VirtIO block driver now rejects malformed completions before copying data,
resets/quarantines queues after ambiguous timeout or malformed completion,
and validates the entire transfer before chunked I/O. Protocol checks reject
unknown commands and unnegotiated flush. PCI capability lengths cannot run
past configuration space. Both driver workflows target this candidate branch.

This is an engineering candidate, not qualified 7.0 or stable 15.0.
See `docs/audit/2026-10-20-consolidation.md` for validation and remaining gates.

## 2026-10-24 supported-device integration checkpoint

Status: **QUALIFIED SUPPORTED SUBSET**, not full 7.0 milestone.
Commit `faceb4526acbbee3bc3ab544d7af76f12132766d` passed
all 26 same-SHA checks in GitHub Actions, including
`qualify-7.0-supported-devices.yml` run `40055252545`:
E1000e and VirtIO-net combined with VirtIO block, USB keyboard/mouse and SMP;
also USB-only boot without NIC or PS/2. Broad USB hubs, physical Realtek NICs
and complete suspend/resume are outside this scoped qualification.

## 2026-10-24 Flux Desktop 8.0 foundation candidate

Window listing now snapshots generation-safe identities and metadata before
user callbacks; stale windows cannot be confused with newly reused slots,
survivors are not skipped and changed z-order does not corrupt iteration.
The desktop foundation gate is an independent 8.0 **engineering checkpoint**,
not a completed 8.0 release. Full graphics mode switching, drag-and-drop,
the general shell API and physical hardware validation remain open.

## 8.0 Flux Desktop Platform — in development (2026-10-24)

Status: **NOT QUALIFIED; formal runtime is still 7.0.0-dev**.

Integrated on `codex/road-to-15-stability`:
- six kernel/SDK shared semantic palettes, user-space palette selection and typed
  `settings.v1` persistence, including session restore;
- Flux Classic / Flux Modern with alternate window chrome, Dock and progress
  rendering; theme-aware wallpaper and retained Ring-3 native scene remapping;
- working Settings mouse-wheel viewport pagination (multi-detent signed safe);
- conservative telemetry: actual PMM RAM frame counts; explicitly labeled
  approximate scheduler/GOP/block-I/O indices, sampled on a shared one-second
  window rather than each GUI client resetting global counters;
- a real user-facing `clipboard.v1` path from Files (copy selected path)
  into Kurogane Web (copy/paste omnibox address);
- C/C++ palette contract, kernel palette/style state, shared metrics window,
  mouse-first app integration and desktop sanitizer regressions.

**Last known broadly green integration baseline:** `17e3382c`
(23 success checks on exact SHA, including host and release media build).
The first scoped USB-only Flux QEMU run at `d6877442` confirmed live login,
USB keyboard/mouse reports and Home but failed to launch Files via an early
QMP dock click; failure was not marked PASS. Subsequent changes delay that
click until `red_flux_home_surface` and use QEMU xHCI USB Tablet, as confirmed
in physical user-run QEMU testing. New exact-SHA tests **must pass** before
counting these fixes.

**8.0 blockers still open**: production text renderer replacing 5x7
bitmap glyphs, dynamic UEFI display modes and UI scaling, general
clipboard editing/shortcuts, inter-app drag/drop, full desktop UI
accessibility, repeatable QEMU interaction with all theme presets and
restart/persistence, and a same-SHA release gate.
Do not bump `common/version.h` or mark 8.0 qualified on code presence alone.

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


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

Status: **REOPENED** after runtime coexistence audit.

Previously qualified automated candidate:
`90d5ffc80f91236e10118324c8e3b5d6f4a1c781`.

Merged through PR #45 as integration commit:
`8374b6ba66a8bb9a4d4ddf8f969816dc789a5be4`.

Qualified Steel scope includes:
- PCI/MSI/MSI-X and bounded I/O APIC routing;
- ACPI MADT, HPET and firmware-derived FADT/DSDT reset + S5 power;
- AHCI plus writable NVMe and xHCI USB Mass Storage block backends;
- xHCI HID keyboard and mouse runtime;
- Intel HDA PCM DMA with AC'97 compatibility;
- unified storage/NIC selection;
- MADT AP discovery, INIT/SIPI AP startup, per-CPU AP state/stacks, Local APIC
  IPI delivery, bounded cross-CPU work rendezvous and synchronous TLB
  shootdown.

Previous exact-candidate Actions evidence on 2026-10-07:
- Steel Closeout `37641425299` — PASS;
- HPET Main Counter `37641425316` — PASS;
- SMP Runtime `37641425398` — PASS;
- Intel HDA Runtime `37641425368` — PASS;
- NVMe Block Runtime `37641425280` — PASS;
- xHCI USB Mass Storage `37641425350` — PASS;
- xHCI USB Keyboard `37641425387` — PASS;
- xHCI USB Mouse `37641425292` — PASS;
- Steel Foundations `37641425390` — PASS.

These runs remain valid for their tested configurations but were incomplete as
a release matrix. A later QEMU audit reproduced:
- xHCI + VirtIO-net MMIO aliasing, with USB still alive while VirtIO networking
  fell back to loopback;
- one-active-slot xHCI behavior preventing simultaneous USB keyboard + mouse.

The repair branch `chatgpt/5.0-audit-fixes` separates VirtIO-net from the xHCI
MMIO window and introduces a dedicated coexistence gate. Multi-device xHCI is
still required before Steel can return to QUALIFIED.

The SMP qualification is deliberately limited to the multi-CPU execution
substrate. Full Scheduler 2.0, per-CPU userspace run queues and process/thread
load balancing belong to 6.0 Core Steel.

USB Mass Storage, NVMe, SMP and Intel HDA are implemented on the current line;
the report that listed them as absent inspected the older `2d550f1` baseline.
Physical-machine/VirtualBox validation remains separate external evidence.

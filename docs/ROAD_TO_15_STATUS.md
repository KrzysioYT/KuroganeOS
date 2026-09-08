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

Status: **ACTIVE**.

Bounded PCI MSI/MSI-X capability discovery exists, but configuration and interrupt delivery do not. Active work begins with bounded hardware-vector ownership and Local APIC delivery/EOI, followed by reversible MSI programming and real OVMF/Q35 interrupt qualification. MSI/MSI-X remain incomplete until that runtime evidence exists.

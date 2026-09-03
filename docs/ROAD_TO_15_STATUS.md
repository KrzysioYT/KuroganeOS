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

Status: **ACTIVE**.

Active work is built on persistent KuroFS allocation primitives over the production `storage::block::Device` contract. The current engineering slice includes revision-checked regular-file growth, zero-filled expansion, truncate-to-zero, contiguous in-place extension, publication-atomic copy-on-write data writes and post-publication extent reclamation. Directory copy-on-grow returns its superseded extent to the allocator. Copy-on-write unlink compacts the parent, refuses non-empty directories, retires the detached inode with an advanced generation and only then reclaims data. Same-directory rename uses copy-on-write, while cross-directory file and non-empty-directory moves prepare both replacement images before recording a redundant-superblock move intent. Remount recovery either retains the old namespace or completes a destination-first publication before exposing the filesystem. Mount refuses invalid live inode metadata, references to free extents, overlapping live extents, stale directory identities and duplicate child ownership. Inode slots now persist explicit pending and orphan ownership, distinguish free slots from generation-carrying tombstones and normalize interrupted namespace attachment during mount. Explicit bounded reclamation tombstones regular-file and empty-directory orphans before releasing their extents, while non-empty orphan trees and ambiguous raw block reservations remain deferred and untouched. Deterministic write/flush interruption, remount, sparse-write, stale-writer, no-space, ownership-transition, orphan-reclaim, tombstone-reuse and reclaimed-range host tests pass.

Native KuroFS runtime persistence is qualified for this slice at exact source SHA `6dd9581e79d79bcd5155b4aa719d7ffcf1a1f8b1` by Actions run `33817447611`. The release kernel mounted a separately formatted raw AHCI disk at `/kuro`; a real Ring-3 program exercised mkdir, create, write, sync, cross-directory rename and stat through the public filesystem ABI. A second OVMF/Q35/KVM process then mounted the same image and verified the persisted payload byte-for-byte. Formal 4.0 same-SHA closeout remains pending together with the remaining Device Model 2.0 and Driver Manager 2.0 runtime gates.

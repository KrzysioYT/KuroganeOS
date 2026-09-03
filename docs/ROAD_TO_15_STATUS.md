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

Active work is built on persistent KuroFS allocation primitives over the production `storage::block::Device` contract. The current unqualified engineering slice now includes revision-checked regular-file growth, zero-filled expansion, truncate-to-zero, contiguous in-place extension, copy-on-grow relocation and post-publication extent reclamation. Directory copy-on-grow also returns its superseded extent to the allocator. Remount, stale-writer, no-space and reclaimed-range reuse tests pass locally; formal 4.0 same-SHA qualification remains pending.

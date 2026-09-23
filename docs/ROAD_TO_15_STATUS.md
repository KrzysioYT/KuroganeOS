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

Current USB baseline: `4b33bfbd05bc8b3f01c1cf89bc44ceabb33da79c` passed
Actions `35231888211` / job `105237832241`, including full host regression,
clean media, 130 HID key pairs, transfer/event ring wrap, empty-controller
cleanup and three held-Shift disconnect/reconnect cycles with fresh handles.
All twenty triggered workflows passed, including Pre-Steel `35231888631`.
First keyboard attachment after empty-controller boot is qualified at
`ad6a19b11d83d0eecaba3599c084321768371e67`: run `35905197560`, runtime job
`107331023235`, cleanup job `107331023071`. Nine triggered workflows passed;
six self-hosted workflows including Pre-Steel `35905199805` remain queued.
The next input slice makes mouse publication all-or-nothing under queue
pressure; this prerequisite does not claim a USB mouse backend.

Single-vector PCI MSI is qualified at exact source SHA `718d8c546b4eb436be382f847f910a62b3714220` by Actions run `34292320432`, job `102281331383`. The production path uses bounded generation-safe vector ownership, validated xAPIC enablement, Local APIC EOI, reversible 32/64-bit MSI programming and ordered route teardown. The runtime proof uses the documented QEMU EDU endpoint to raise and acknowledge a real device MSI; QEMU's 82540EM `e1000` remains on its truthful polling/INTx-compatible path because that emulated model does not expose MSI.

As recorded in the master roadmap, bounded single-vector MSI-X was subsequently qualified at `745793376abf6cb5f88a4410236b4b2ad6a2c1ca` in Actions run `34358861039`, job `102490389818`, and production VirtIO-net shared RX/TX MSI-X adoption passed at `6898c72f273207dbfd526f78a640433932e2291c`, run `34683815908`, job `103527144398`.

Bounded MSI-X route groups passed exact-source Actions run `34811940628` at `d42d9e31e98fbcb39577184475e36d97cb77d251`, together with Pre-Steel closeout `34811940844`. The contract and evidence are tracked in [PCI_MSIX_GROUPS.md](PCI_MSIX_GROUPS.md). The production VirtIO RX/TX group extension is now qualified at exact source SHA `308f2fb75205dfa265c8a620cf88e84d0627742d`: Network Cleanup run `34950706350` / job `104320726734` and MSI-X Runtime run `34950706505` / jobs `104320726926`, `104320727235` passed. These gates exercise malformed used-ring rejection, no-partial-mutation ownership, legal 16-bit completion wrap, active-queue reset/cleanup, real queue IRQ delivery, two-vector delivery, and truthful no-MSI-X polling fallback with clean release-media builds. The APIC runtime now owns a bounded fixed-delivery I/O APIC route contract: validated hardware-vector selection, masked programming order, destination encoding, GSI range checks and explicit clear-before-reuse. ACPI MADT ISA overrides are now resolved through an allocation-free legacy IRQ→GSI contract with conforming defaults and malformed/conflicting flag rejection. Current-head host evidence includes `[test_io_apic_irq] PASS` in Fatal Diagnostic host job `104330260616` (Actions run `34953615582`); real I/O APIC interrupt delivery and SMP remain open. The route layer also rejects overflowing or overlapping I/O APIC redirection GSI spans before a candidate is retained; host span/route coverage remains part of the green host suite. SMP and broader hardware qualification remain open. Steel is not yet qualified.

# Repository and system audit — 2026-10-27

Scope: current roadmap integration plus Flux work on
`codex/flux8-roadmap-20261027`. Runtime identity remains `7.0.0-dev`.
This is a bounded source and execution audit across subsystems, not a claim
that every code path or all hardware has been exhaustively verified.

## GitHub cleanup and recovery

Re-enumerated 63 branches and 11 open PRs. Deleted 39 branches only after
proving each tip is an ancestor of a retained tip. Kept 24: main, all open PR
heads/bases, protected branches and representatives with unique history.
Deletion was atomic with an expected-SHA lease on every ref; remote heads and
open PRs were rechecked immediately before the push. Main was not modified.
Names, exact SHAs and retained containing branches are in
`2026-10-27-deleted-branches.json`. A verified `pre-cleanup.bundle`, full
inventory, push receipt and final remote refs are retained in the delivered
GitHub audit folder. Restore a ref with `git push origin SHA:refs/heads/NAME`
using its recorded pair; commits remain reachable in the retained branches.
The bundle is an independent backup, including all refs at capture time.

## Pull request review

- #56 had conflicts after eight new base commits. Integrated the latest base
  using a merge, preserved FAT32 path hardening and both test suites, and fixed
  a stray quote in the host runner. No history was rewritten.
- #55 is the integration PR into `gpt/road-to-15-consolidation`; its history
  already contains the tips of #51, #52, #53 and #54. These PRs are overlapping
  historical stages, not four additional independent integrations. Review and
  close/retarget them deliberately after integration acceptance.
- #53 and #54 both target Core Steel; their histories are already in #55.
- #7, #9, #10, #11 and #12 still have commits absent from current development.
  Their old bases and #10 -> #11 -> #12 stack require patch-level review.
  #9's GUI branch must not be merged wholesale over current Flux/ABI.
- Eleven other retained branches contain unique commit histories (HID, HDA,
  NVMe, pointer input, Core Steel, audio and GUI/font work). Commit counts are
  not proof of unique functionality; patch-equivalence review is still needed.
  No such branch was deleted or automatically merged.

Recommended structure: main for reviewed integration only, one roadmap
integration branch, and short feature branches feeding it. Finish #56 into
#55's branch; qualify #55 before considering the next integration. Archive
obsolete branch names only after reachability/patch review and recoverable SHA
capture. Avoid creating parallel replacement roadmaps.

## Subsystem review and open findings

| Area | Reviewed evidence / tests | Finding and remaining scope |
| --- | --- | --- |
| Kernel and architecture | x86-64 UEFI, context-switch assembly, `thread.cpp`, schedulers; legacy thread/process suite | Real switch code now runs in Mach-O host tests; no guest instruction replacement. Both scheduler generations exist; do not delete either without call-site migration. |
| Devices/drivers | generic registry/requirements, DMA ownership tests, HID, AHCI/NVMe, PCI power, audio | Optional hardware returns explicit unsupported status. A protocol test does not qualify all physical controllers. |
| Memory/processes | allocator, VM mapping, ELF loader, scheduler, shared memory; new UBSan script | Fixed limits include 16 processes and 16 threads. Resource exhaustion and full application coexistence need larger guest stress runs. |
| Filesystems | FAT32/VFS/KuroFS tests, package parser, mixed-case live lookup | Integrated rejection of casefold aliases/file-directory collisions and live/installed FAT32 parity. Crash-recovery coverage is scoped to tested adapters. |
| Security/syscalls | `validate_user_buffer` checks range subtraction and each mapped user/writable page; stale handles and session-owner tests | Product password KDF is still missing: installer emits `FNV1A64-DEV`. Do not treat it as password-hardening suitable for stable. Full concurrent syscall fuzzing and privilege review remain open. |
| Userland/ABI | SDK ABI, ELF inventory, external project generator | Build verifies 44 required ELF payloads. No ABI renumbering introduced. |
| Flux/window manager | actual QMP transitions, owner PID/window generations, resource counts | Settings now supports visible keyboard focus and activation. Licensed scalable typography, HiDPI, complete clipboard/app/crash matrix still open. |
| Installation/updates | exclusively created scratch image, cancellation hash, two disk-only boots | Install qualification covers English user/no password/AHCI. Transactional upgrades, signed update/rollback and power-cut tests are not qualified. |
| Settings | service SET/GET, temporary file, sync, rename; exact theme reboot test | Existing replacement fallback unlinks the old DB before rename: a power cut in that gap can lose the durable name. Requires atomic replacement/recovery design and fault injection; not fixed by a normal reboot test. |
| Developer tooling | both host runners, shell syntax, strict logger check, release/debug builds | Fixed HTTPS fixture integration, Mach-O assembly portability and legacy SDK generator selection of a host Mach-O linker for an ELF target. Added reusable core UBSan gate; retained real source assertions. |
| Documentation | authoritative master roadmap vs dated historical reports | Historical PASS is revision/profile specific. Current release remains unqualified; redundant milestones are not new releases. |

No unused production code was removed based on grep results. Unsupported
statuses and historical scripts are not automatically dead code. No logo or
branding change was made.

## Evidence policy

The delivered test matrix records PASS/FAIL/BLOCKED/NOT RUN, tested revision,
media hash and logs. Reproduced failures are retained alongside their fixed
reruns. VirtualBox 7.2.14 on Apple M4 rejects x86 guest launch with
`VBOX_E_PLATFORM_ARCH_NOT_SUPPORTED`; its gate is BLOCKED, not a QEMU PASS.
Physical hardware, native Windows/Linux builds, power-loss tests, TSAN and
exhaustive syscall fuzzing are NOT RUN locally unless separately evidenced.

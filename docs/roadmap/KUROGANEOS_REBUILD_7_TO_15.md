# KuroganeOS native rebuild — 7.x through 15.0

Status: **ACTIVE CANDIDATE / NO PRODUCT STABLE RELEASE**
Source of truth: `DEVELOPMENT_STATE.md`; work branch
`codex/road-to-15-stability`; PR #55 kept Draft until qualified.

## Non-negotiable principles

1. Keep KuroganeOS's own x86-64 UEFI kernel, not Linux, Wine or a superficial
   shell on Windows. Modern compositor concepts need not implement Wayland.
2. Deliver atomic **user-visible feature slices**: describe the journey ->
   design -> implement -> build -> targeted host test -> document -> commit.
   Run QEMU/compatibility-VM and full regressions at integration checkpoints
   and before version promotion; an unavailable VM does not stop unrelated
   feature work. Never turn an unverified result into a PASS.
3. Keep the last qualified kernel/driver baseline and a working Classic UI
   fallback. Never force-push, delete VDI data, or blindly change boot media.
4. A CI startup PASS marker is not functional proof: require an actual
   authenticated user input, native window creation and launched child.
5. Keep release identity in `common/version.h` unchanged until every
   milestone's exact SHA has passed its explicit acceptance matrix.

## Ordered technical workstreams

| Workstream | Next objective | Gate |
| --- | --- | --- |
| Core/kernel | Process lifetime, syscalls, paging, SMP scheduler, lock discipline | sanitizer/host + SMP/UEFI QEMU |
| Devices | HID keyboard+mouse, NVMe/AHCI, networking, audio and hotplug | coexistence + failures, no PS/2 assumptions |
| Disk/install | recoverable storage, VFS, install to blank virtual disks | safe install/verify/reboot with exact media |
| Accounts/security | Login, session owner, per-process UI permissions, stale handles | no login bypass, spoofed window or hidden fallback |
| Services | notification/settings/clipboard/IPC backpressure | crash, reconnect, saturation and restart |
| Flux compositor | independent app surfaces, z-order, capture, cursor, damage, scaling | real mouse/keyboard+multiwindow smoke |
| Typography | Unicode decoding -> scalable licensed font -> text shaping/HiDPI | Polish labels, clipping, sizes, malformed UTF-8 |
| Applications | canonical IDs, launch/focus, terminal/files/browser/perf/settings | click + shortcut -> real process and window |
| User experience | Modern consistency, dock, layouts, access keys, recovery | QEMU and VirtualBox manual acceptance |
| Tooling | deterministic ISO, installer/test images, CI artifacts and logs | SHA/media hash and reproducible failure logs |

## Milestone gating and scope

- **7.x — Iron Shield**: finish and requalify hardware coexistence, SMP,
  interrupt/drivers and security integration without claiming physical
  hardware coverage from QEMU alone.
- **8.0 — Flux Modern**: clean Login->Home and useful Files/Terminal/Browser;
  coherent Red Flux application design, personalization, native launcher
  search, accessibility, scalable fonts/HiDPI, real client surfaces and a
  basic external-GUI-app developer path. Release still requires exact-SHA
  guest/security/recovery evidence. See `FLUX8_MODERN_COMPLETION.md`.
- **9.0 — Networking & Services**: networking reliability and expanded services.
- **10.0 — Storage & VFS 2.0**: persistent storage, filesystem and migration safety.
- **11.0 — Application Platform**: SDK, app lifecycle, packages and native utilities.
- **12.0 — Security, Updates & Recovery**: permissions, credential security,
  signed updates, rollback and recovery.
- **13.0 — Performance & Power**: measured performance, scaling and power lifecycle.
- **14.0 — Release Candidate**: security audit, regression closeout,
  performance/thermal bounds and installer rollback drills.
- **15.0.0 stable — public target**: all mandatory evidence green on the
  exact release SHA; signed/verified media, release docs and user-visible
  version identity after the pass.

These are goals, not statements that these milestones exist or have passed.

## P0 guest acceptance (must pass before further version promotion)

- Fresh release ISO from known SHA and recorded hash, clean UEFI boot.
- Authenticated Login's actual UI first frame, then Home first frame.
- Left click Dock Files -> Ring-3 child process and interactable Files scene.
- Keyboard shortcut -> Terminal process and responsive Shell, not just PID log.
- Reopen existing window -> focus/restore; no duplicate or missing Dock state.
- Window close/minimize/resize/drag, clipboard across apps, wheel events.
- Controlled launcher failure -> visible and **authorized** recovery,
  never a blank desktop or a hidden shell.
- QEMU USB-only mouse+keyboard and VirtualBox PS/2 guest confirmation.
- Host optimized/UBSan tests, clean media build, no failing exact-SHA jobs.

## Evidence discipline

Track results in `DEVELOPMENT_STATE.md`: stage, source SHA, job/run ID,
ISO hash, profile, result, whether physical or virtual, open blockers.
A source change resets the relevant gate: old green checks do not certify
new source. Do not merge the Draft PR on the strength of appearance alone.

## Changes integrated on 2026-10-24

- Native 4x4 rounded corner coverage and unified Modern controls.
- Bounded UTF-8 text cells and interim Polish glyphs.
- Shared kernel/Home/Ring-3 application title/path catalog.
- Added host/CI tests for Unicode fitting and application identity.
- This implementation is **in development** pending CI and guest evidence.

## 2026-10-27 — host toolchain portability regressions (active)

The exact-SHA CI at `58c98d62` had 24 failed checks but not 24 distinct
subsystem regressions. Multiple hosted C++ driver builds stopped at the
legacy freestanding kernel `namespace log` spelling colliding with the
compiler math builtin under `-Werror`. A scoped diagnostic suppression in
`kernel/core/log.hpp` keeps all other warnings enabled. A TLS HTTP host
harness now supplies its missing kernel diagnostic sink and checks that an
error path actually uses it. Both changes require new CI confirmation;
none proves hardware qualification or finished 15.0.0.

## 2026-10-27 — installer FAT32 collision safety (active)

Package validation now treats two paths on the same destination volume as
colliding when they are case-insensitive aliases or one path is the parent
of another file. Formerly, only byte-identical paths were rejected, which
could create ambiguous or unreachable files in the FAT32 installed root.
The Python producer and freestanding C++ package parser enforce the same
rule, while correctly allowing the same path on separate ESP/root volumes.
Tests cover both prefix directions, case differences and noncolliding names.
This hardens the future 10.0 storage and 12.0 update/rollback foundation,
but is not evidence of those milestones' completion.

## 2026-10-27 — Live Try package FAT32 name equivalence

The read-only package VFS now resolves paths and deduplicates nested directory
entries using ASCII case-folded FAT8.3 comparisons, matching the installed
FAT32 root. Mixed-case package trees no longer yield duplicate `APPS/apps`
directories or missing child files. Host package VFS tests exercise root and
nested enumeration, mixed-case lookup/read and read-only operations. This is a
platform consistency fix, not evidence that the storage milestone is complete.

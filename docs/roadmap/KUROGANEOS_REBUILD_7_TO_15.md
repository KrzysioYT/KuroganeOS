# KuroganeOS native rebuild — 7.x through 15.0

Status: **ACTIVE CANDIDATE / NO PRODUCT STABLE RELEASE**
Source of truth: `DEVELOPMENT_STATE.md`; work branch
`codex/road-to-15-stability`; PR #55 kept Draft until qualified.

## Non-negotiable principles

1. Keep KuroganeOS's own x86-64 UEFI kernel, not Linux, Wine or a superficial
   shell on Windows. Modern compositor concepts need not implement Wayland.
2. Progress in atomic slices: audit -> test that reproduces -> implement ->
   build -> QEMU -> compatibility VM -> regression -> commit.
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
- **8.0 — Flux Modern**: clean Login->Home; functional native applications,
  modern widgets; bounded UTF-8; scalable fonts; per-client surfaces; HiDPI;
  fallback console. See `FLUX8_MODERN_COMPLETION.md`.
- **9.0 — Platform UX**: display/input settings, multi-resolution/window
  state persistence and robust hotplug/focus handling.
- **10.0 — Application Platform**: app registration, lifecycle, permissions,
  package integrity and native application SDK usability.
- **11.0 — Files & Persistence**: stable writable FS, safe updates, file
  dialogs, recovery and migration tests.
- **12.0 — Connected System**: DNS/TCP/TLS, secure services, failure-restart
  behavior and audio/network integration.
- **13.0 — Hardware Expansion**: wider UEFI/ACPI/PCI compatibility and
  measured runtime behavior across supported emulator profiles.
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

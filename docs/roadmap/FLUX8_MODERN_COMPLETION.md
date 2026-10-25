# Flux Modern 8.x — Completion contract

Status: **ACTIVE DEVELOPMENT, NOT QUALIFIED**
Branch: `codex/road-to-15-stability` (Draft PR #55)
System identity remains `7.0.0-dev` until exact-SHA acceptance.

## Non-negotiable reliability gate (P0)

- Real authenticated Login -> live `RED FLUX HOME` first frame.
- Dock/Home/desktop shortcuts launch a Ring-3 process, not just show startup PASS.
- Click, keyboard, focus, restore, minimize, close, resize and wheel across apps.
- No blank desktop on failed Login/Home: visible retry or PID1-only recovery shell.
- Zero privilege escalation via fake `KUROGANE LOGIN`/`RED FLUX HOME` window titles.
- Exact-SHA release media, QEMU USB-only login and actual Files click, VirtualBox
  interactive smoke, and host optimized/UBSan gates all pass.

## Native compositor and Modern style (P1)

- [x] Own software backbuffer; preserve clipping, damage and non-XOR cursor.
- [x] Floating compact Dock and Classic fallback.
- [x] Rounded Modern chrome, cards, switches, notifications, list and progress.
- [x] Deterministic 4x4 corner anti-aliasing and bounded labels.
- [ ] Explicit per-client surface lifecycle independent of title strings.
- [ ] Robust focus/capture/z-order and no input event loss under heavy redraw.
- [ ] HiDPI layout factor 100/125/150/200%, fractional scaling without clipped
      widgets, resolution change without login/logout or stale pointer position.
- [ ] Dirty-region and cursor performance telemetry; memory/budget limits.
- [ ] Optional future GPU backend behind same compositor interface.
  **Do not** claim Wayland protocol compatibility without implementing it.

## Typography, accessibility and usability (P1)

- [ ] Licensed, bundled scalable sans font rasterizer (not the 5x7 console font).
- [ ] UTF-8 decoding, Latin/Polish diacritics, replacement glyph and text layout.
- [ ] Anti-aliased normal/bold weights with a bounded cache and CPU budget.
- [ ] Text fit and ellipsis preserve code-point boundaries at every scale.
- [ ] Keyboard focus visibility, logical tab order, input edit/caret, mouse wheel.
- [ ] Contrast validated in all six themes. Reduced-motion fallback.

## Desktop application integration (P1)

- [ ] Home, Terminal, Files, Forge Control, Web, Monitor, Settings, About and
      Notifications launch/focus from both mouse and chorded keyboard access.
- [ ] Native app rendering follows Modern styles; Classic can be chosen and
      remains functional across restart.
- [ ] Clipboard, persistent window settings, application registry and file
      dialogs have live integration tests, not just fake labels.
- [ ] Crash/restart ownership cleanup does not leave stale windows or frozen Dock.

## Release gate / acceptance

Do not label 8.0.0 stable just because theme switch and wallpaper render.
The checked build must pass: optimized + sanitizer host UI suites, clean media,
real USB keyboard/tablet QEMU login and Files/Terminal child spawns, VirtualBox
manual mouse/keyboard test on the same ISO, and failure/recovery tests.
Record exact SHA, ISO hash, runner logs, and reproducible commands. New failures
block promotion. Windows support, PS/2 fallback, and Classic are retained.

## Current working slice (2026-10-24)

- Software rounded surfaces and corner coverage.
- Consistent Modern top bar, list, notification and progress surfaces.
- Width-bounded Modern labels.
- Regression smoke requires an actual authenticated Files launch.
- Main blocker remains font implementation and unqualified runtime/session
  behavior. Continue reliability and typography before declaring completion.

## Text foundation, 2026-10-24

- [x] Small freestanding UTF-8 scalar scanner with invalid-input replacement
      and code-point-safe, bounded label truncation in Modern UI.
- [x] Interim Polish glyph fallback (ĄĆĘŁŃÓŚŹŻ upper/lower) in kernel
      software bitmap text; unsupported glyphs render '?' as one cell.
- [ ] Replace transitional 5x7 raster with licensed, scalable anti-aliased
      fonts, shaped text, full Unicode and proportional layout.
- [ ] Host/QEMU exact SHA green gates and VirtualBox first-frame acceptance.

## Application registry refactor — 2026-10-24

The pinned Dock previously expected `FLUX TERMINAL` while the actual
Terminal process created `RED FLUX TERMINAL`; app focus/running detection
therefore failed even if the process existed. Kernel, launcher and native
Ring-3 windows now share title/path declarations in
`sdk/include/kurogane/flux_app_catalog.h`; host C/C++ and source parity
tests reject drifting titles. The catalog conveys identity metadata only:
PID ancestry and session authorization remain mandatory for privileged UI.

## Ring-3 app readiness / recovery increment — 2026-10-24

Home now queries an append-only read-only desktop-pin action for a real
presented app surface. A successful process spawn does not imply a usable
window. Home focuses an already-ready window, holds a pending child for an
initial 15-second grace, and allows the **user to click again** after the
grace to retry a stalled child process. Only the session-owned direct child
can be terminated through the preexisting child termination API. The
append-only request layout and SDK syscall number remain unchanged;
C11/C++17 and UBSan policy tests cover timeouts and monotonic clock edges.
Normal slow launches are not automatically killed by a background thread.
This is a reliability slice, not live VirtualBox qualification.

## Extended QEMU live acceptance (2026-10-24)

Fixed a shell line-continuation regression in the scoped Flux 8 QEMU workflow:
comments placed after a backslash-continued option terminated the command,
so the run could fail without ever sending the intended Files click.
The single QEMU invocation now verifies **both** Files after login and the
Terminal after a real Files wheel event; requires two distinct child spawns
and a Terminal Ring-3 marker. The static test rejects future comments in
continued shell option lists. This gate must pass on the exact SHA before
considering any release promotion.

The guest wheel gate distinguishes **input delivery** from actual page
movement: a root directory with <= PAGE_SIZE entries must not fail merely
because scrolling cannot change its page. The host paging-policy test still
covers offset changes separately. The QEMU command's static shell checker
now uses a scoped, syntactically valid assertion.

The Home Deck running badge now requires a **presented** native app surface,
not just a live child PID. A once-per-second observation emits a first-frame
marker for each ready app. The exact-SHA QEMU check requires two child spawns
and two compositor-observed surfaces (Files + Terminal); the app creation
marker alone is insufficient evidence of working UI.

Stalled child recovery now waits up to 200 ticks for a real child reap before
reusing its launch slot; failed waits keep the child tracked. Missing monotonic
time is an explicit unknown sentinel and never triggers forced termination.


## 2026-10-25 — CI compiler blocker and visible Login stage

The latest exact-SHA CI failure exposed a root-level compilation regression:
`kernel/ui/window_manager.cpp` included `flux_app_catalog.h` only in the
non-host block but its pinned Dock table uses these symbols in both host
and guest builds. The shared metadata header is now included unconditionally.
The C/C++ keyboard-shortcut test uses valid aggregate syntax for both
languages to avoid `-Wmissing-field-initializers` failure under C++17.

After a failed authenticated Login handoff, the on-screen error now reports
the **specific failed stage** (ACCOUNT LOOKUP / SESSION SERVICE / CREATE
SESSION / LAUNCHER SPAWN / SET HOME / ATTACH HOME / VERIFY HOME / HOME
FIRST FRAME / CLEANUP), without exposing secrets. COM1 still contains the
full bracketed status markers. The screenshot alone did not identify which
stage failed on the previous media; live VirtualBox verification is open.

## QMP logical pointer coordinates vs GOP screen coordinates

The scoped QEMU smoke had fed physical 1280x800 pixels into the helper,
but its absolute USB tablet movement explicitly scales input from the
1024x768 QMP logical coordinate range. This can reliably deliver a primary
button but miss every Dock button, so the runtime only reports mouse input.
Files is now targeted at logical (450,726) and Terminal (367,726), within
their respective pins (Terminal after Files changes the task count) for both 1024x768 and 1280x800 GOP. The wheel point
is independently inside a Files client window. The static test enforces
QMP scaling assumptions and rejects the old raw-pixel positions.

A persistent installed account may differ between Login and accountd.
The account lookup now emits separate no-secrets COM1 markers for connection
failure, service reply failure and profile mismatch, plus a confirmed match.
This distinguishes a stale installed profile from a launcher/UI failure without
logging usernames, hashes, passwords or personal account data.


## 2026-10-25 — Verified VBox account lookup failure

User-supplied VM COM1 output repeatedly reports
`[SESSION] flux_account_lookup: FAIL`, while Login surface, physical
pointer events and retry-screen rendering pass. Therefore the session
manager, launcher and Home are not being reached. **Exact subcause is not
yet identified**: the earlier ISO did not emit separate connect/reply/profile
markers. Non-sensitive diagnostics now distinguish accountd's installed/live
profile state, a malformed installed config, IPC send/receive/timeout, ABI
response errors and service rejection. The PowerShell verifier now prints
PID1/accountd startup from the beginning of the boot log even when thousands
of retry clicks push those markers beyond the tail window. It does not bypass
login or weaken installed account validation.

## 2026-10-25: diagnosing VirtualBox accountd restart loop

The user's live COM1 log shows installed-first-boot PASS, PID1 accountd
spawn PASS, **three accountd restart markers**, a later supervisor liveness
PASS, and repeated Login account lookup FAIL. There is **no accountd ready
marker** in that logged old ISO. Restart + process spawn is not a service
response or a successful account lookup, so status remains P0.

New COM1 startup markers delimit accountd ELF entry, IPC binding, and profile
load; supervisor logs each service's child exit kind/code and an exhausted
restart budget. A new ISO is needed for these markers. The root cause
(service bind, fault or invalid installed root) is not yet proven; keep
account verification intact and never infer login success from liveness PASS.

### 2026-10-25: common daemon runtime failure isolation

VirtualBox installed root logged supervised restarts of accountd, sessiond,
notificationd and other services, not only an account profile failure.
The earlier successful isolated Ring-3 page-fault test occurs before daemon
startup and is not evidence of the cause. On Linux/QEMU media accountd ran,
but VirtualBox had no `accountd: ... online` marker. The process image
runner now reports the exact **installed executable path** and a bounded
runtime failure category (file absent, bad ELF, OOM, address space or stack)
when a daemon fails before entering Ring-3. Unexpected user-mode faults are
reported with their vector. PID1's supervised exit code remains separate.
This is diagnostics, not a verified runtime repair; don't reset installed
credentials or delete the VM until the fault has been isolated.

## 2026-10-25 — Current acceptance checkpoint

`7f06237` passed the host sanitizer and real QEMU USB-only Login/Home,
Files and Terminal jobs in run `40163835705`. The Terminal target is (367,726)
after Files recenters the Dock; the production dispatch test covers both GOP
sizes. This is scoped evidence, not completion of every checkbox above.

`9c8700c` restores Windows/Linux parity for 33 baseline executables, including
all PID 1 services, with scalar flags and disassembly inspection. Five host
regressions and Linux release compilation passed. The scoped Flux 8 workflow
`40164065199` also passed on `9c8700c`, including PowerShell parsing. Native Windows/VirtualBox
acceptance, including the old installed-root incident, remains open.
[Current status](../../DEVELOPMENT_STATE.md) takes precedence over dated
working-slice notes. No version promotion is implied.

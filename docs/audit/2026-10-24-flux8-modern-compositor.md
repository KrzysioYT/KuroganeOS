# KuroganeOS Flux 8 — Modern compositor migration (2026-10-24)

## Why
The Classic/Modern toggle previously changed only rectangular widget chrome.
The desktop still used a 5x7 bitmap typeface and a framebuffer XOR triangle
cursor. The attached VirtualBox screenshot demonstrates the old appearance.

## Implemented here
- Replace the XOR cursor with a non-inverting, framebuffer-format-aware
  software cursor overlay. Each modified GOP pixel is backed up and restored
  when the cursor moves or a compositor frame begins. The input coordinates,
  event ordering and mouse capture semantics remain unchanged; no pointer
  smoothing or additional input latency is introduced.
- Use a bounded cursor mask, 64-bit clipping arithmetic and tests for
  edges, corners, offscreen coordinates and framebuffer restoration.
- Add freestanding integer-only rounded-rectangle raster geometry that honors
  the existing clip and damage rules through fill_rect.
- Render Modern window backgrounds, dock, dock buttons, panels and Home app
  tiles with subtle rounded corners and restrained focus accents.
- Keep Classic's existing angular look as an explicit fallback.

## Verification
- New host tests: test_flux_cursor_overlay and test_flux_rounded_geometry.
- Run these in scripts/run-host-tests.sh and the scoped Flux desktop
  qualification as optimized and UBSan builds.
- The scoped workflow still requires real QEMU/OVMF Ring-3 USB-only desktop
  startup with both keyboard and pointer.
- Human acceptance: mouse move on bright and dark backgrounds must no longer
  invert or corrupt background pixels; move/resize/close windows, drag across
  screen edges, toggle Modern and Classic and inspect each theme.

## Not yet implemented or claimed
- This is KuroganeOS native compositor work **inspired by modern Wayland
  desktops**, not an implementation of the Wayland protocol.
- The 5x7 bitmap glyphs are **not** a scalable, anti-aliased font engine.
  True font atlas/rasterizer + UTF-8, HiDPI, per-client surfaces and
  proper fractional-scale layout remain future 8.x milestones.
- There is no hardware GPU acceleration or full dynamic modesetting here.
- Do not mark 8.0.0-dev or 15.0 stable without exact SHA CI and manual
  VirtualBox acceptance; keep 7.0.0-dev version and PR #55 draft.

## Further Modern widgets

The retained/native compositor's Modern style now covers app actions,
text inputs, system metric cards and toggles with soft corners, reduced
high-contrast strips and modern switch tracks. Classic is unchanged. The
integer raster rounds use 64-bit intermediates to avoid signed i32 overflow
at extreme coordinates. These remain software-GOP surfaces, not Wayland or
GPU-accelerated clients.

## Input and Dock functionality recovery

- APPS no longer minimizes the session-root Home window when already focused.
  This removes an easy way to hide the command broker for every app tile.
- Floating Dock work-area reservation is now 18 px, down from 82 px.
  The 58-px floating Dock appears on pointer proximity and collapses to a
  52x3-px bottom handle when the pointer leaves. Geometry/hit-testing share
  the same `calculate_workspace().pulse_ribbon` source.
- Added Ctrl+Alt+F1…F8 keyboard rescue chords to focus/launch Home,
  Terminal, Files, Forge, Web, Monitor, Settings, and About. Normal physical
  typing and unchorded desktop keys remain with the focused Ring-3 client.
- First eight real primary mouse button-down events write a runtime marker
  to serial to distinguish driver/input delivery from UI hit-testing.
- `tests/test_window_manager.cpp` now checks floating Dock bounds/visibility,
  HOME persistence on Dock clicks, and Ctrl+Alt+F1 restoration. This is host
  logic evidence; QEMU and VirtualBox guest verification remain required.

Host test isolation: the hovering-window partial-raster test first consumes any
floating Dock visibility transition; otherwise expected one-window damage is
contaminated by a legitimate Dock full-frame transition.

## User-side VirtualBox diagnostic command

After refreshing ISO and making real clicks, run
`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\check-flux8-ui.ps1 -Name "KuroganeOS-Flux8-Modern"`.
It reads COM1 under `$HOME\VirtualBox VMs\...` without mutating the VM,
shows which stages actually happened, and does not confuse missing markers
(before user interaction) with proven regression failures. New runtime
`[INPUT]` traces are emitted at mouse delivery, launcher action receipt,
Home tile hit and child process spawn.

## Atomic Login-to-Home hand-off — 2026-10-24

A user verified the native desktop appears but the Home window and every
application command are missing. The kernel Dock forwards app requests through
the `RED FLUX HOME` Ring-3 owner; when it is missing, desktop clicks and even
Ctrl+Alt+F1…F8 are ineffective. The specific reason Home did not present in
VirtualBox remains subject to COM1 diagnostics.

The login process previously closed its own window **before** spawning and
validating `/gui/launcher`. That made a blank desktop possible on a failed
handoff. Login now retains its window until the actual Home window has both
been created and submitted a retained surface, verified via a read-only,
ABI-compatible `KU_DESKTOP_PIN_HOME_READY` action. The query is bound to
Home ID 0, doesn't mutate pin state, and cannot promote session credentials.
Waiting is bounded (2500 ticks) and aborts early if the child exits; stage
failure markers are serialized rather than optimistic startup PASS labels.

Host regression covers nonpresented Home, first surface, minimized state,
restoration and window destruction. Manual VBox/UEFI and exact SHA CI remain
necessary before claiming live functionality.

No-Home renderer now shows a visible "SESSION HOME NOT READY" diagnostic
when neither a Login screen nor a Home window exists. It does not attempt a
privileged kernel-side bypass of login/session ownership. VirtualBox COM1
helper auto-selects the latest VM log if -Name is omitted and reports
launcher process creation, session verification, Home first frame and login
handoff separately.

## 2026-10-24: Login failure recovery

Prior login code returned from main on failure to connect to accountd/sessiond,
or to create/present Home. That destroyed Login before init could reliably
restart it, leaving a GUI with no real launcher. Negative pre-handoff errors
now keep the Login window alive, clear the submitted password and allow a click
or Enter retry on the same process, showing a visible error instead of
silently exiting. Added serial checkpoints at account lookup, session create,
HOME registration, launcher window creation and first frame.

This prevents one blank-session mechanism; diagnosis of the actual user's guest
still requires its serial log and a full VirtualBox/QEMU execution.

## Login retry and session-root correctness

An installed-profile valid credential that hits a Home startup error now
retains the useful retry error instead of falling through to INCORRECT PASSWORD.
PID 1 retries a prematurely exited login gate up to four bounded attempts
rather than immediately handing off to an invisible serial shell. Home root
ownership is committed only after a real window slot is allocated, preventing
an unsuccessful allocation from poisoning subsequent application ownership.
The fallback remains a last resort after bounded attempts; there is no
unauthenticated kernel shell launch of GUI apps.

## Early Home first frame — avoid service-dependency startup deadlock

Launcher now creates and presents its first usable Ring-3 native Home scene
before restoring theme/style from settingsd, reading its persisted pins or
publishing a notification to notifd. These optional dependencies can take
multiple bounded waits on slow emulated hardware, so they must not block the
authenticated first-frame handoff. A post-service refresh failure is
diagnosed but cannot close an otherwise healthy Home application. The UI
still performs later normal event-loop refreshes.

Expected serial ordering: `flux_home_ui_present: PASS` occurs before
theme/style/notification restore markers. Real emulated-boot proof remains
outstanding for exact commit.

## Parent-only launcher cleanup

New ABI append-only `KU_SYS_TERMINATE_CHILD = 71` is a restricted process
termination syscall. Kernel verifies the target is a live immediate child
owned by the invoking Ring-3 process; self/PID0/system/sibling termination is
rejected. Login uses this exclusively on a pre-handoff failure, waits a
bounded interval for process release, and refuses an unsafe retry if an
orphan launcher could survive. This prevents duplicate Home windows and
stale desktop session roots after partial sessiond setup.

Not a general process killer and not an authorization bypass. Static SDK
ABI test asserts the append-only number. The full VM runtime must still
confirm teardown and restart on target hardware.

## Termination permission regression & COM1 diagnosis

The Ring-3 terminate-child syscall uses the pure production policy
`user::child_termination_policy::allowed`. A 125k-combination host test
covers parent-only access, denial of self/PID1, sibling and orphan processes.
Host suite and scoped Flux UBSan workflow compile it. COM1 diagnostics now
list account lookup, session registration, Home creation/first frame, Login
handoff, recovery and child cleanup (5000-line tail). Guest qualification
is still required.

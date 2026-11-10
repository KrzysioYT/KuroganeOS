# Flux Modern 8.x — Completion contract

Status: **RELEASE BLOCKED**. [Stan kandydata i otwarte bramki](../releases/FLUX_MODERN_8.md).
Integration branch: `integration/flux8-reconciliation`
Draft integration PR: #58 (ancestry #55 -> #56 -> #57)
System identity remains `7.0.0-dev` until exact-SHA acceptance.

## Candidate qualification — `d0bb7511`

The exact candidate ISO passed the nine exercised scenarios in visible
VirtualBox with PS/2 input. Separate QEMU coverage used USB HID without PS/2.
The GOP profiles were selected before boot. In-session resolution changes,
full desktop scaling, Dock ordering and saved window geometry remain open
contract items. [VirtualBox evidence](../FLUX8_VIRTUALBOX_MATRIX.md)

## 2026-11-07 — restored P0 qualification and native input fix

The [development scorecard](../../DEVELOPMENT_STATE.md) records exact-SHA
evidence. The integration branch runs the dedicated USB-only Flux workflow.
It clicks the Modern rail in the real live ISO, then tests installed Login
with wrong/correct passwords, two disk-only boots, and recovery with the
account files removed from a disposable copy. A damaged installed profile
does not select the passwordless developer fallback.

Native command boundaries and pointer mapping agree at all four scales in
host tests. At 200%, the QEMU lifecycle harness clicks the last painted
pixel of a Files tile, confirms real directory navigation using history,
and verifies restoration after reboot. This covers one guest hit target,
not the visible layout and access to every control at each scale.

The interactive VirtualBox gate is still open. Use the matching ISO, SHA256,
and [manual acceptance steps](../VIRTUALBOX.md); do not count headless input
or screenshots alone as manual acceptance.

## Historical runtime qualification — `64577459`

Runtime source commit:
`6457745962fde73c81a7004bce13cea6992a85c1`; PR #58 remains Draft and the
runtime stays `7.0.0-dev`. Persisted 100/125/150/200% scale now applies to
native Ring-3 command geometry as well as text; pointer and scroll coordinates
are inverse-mapped so app hit testing continues to use logical coordinates.
This is partial HiDPI only: app-icon internals, desktop/window chrome and
display-mode changes are not scaled.

| Evidence | Result |
| --- | --- |
| Full optimized host regression | PASS |
| Core UBSan | PASS — 11 groups |
| Exact-SHA Debug and Release kernel/userspace/SDK/ISO builds | PASS |
| Release ISO structural validation | PASS — 20 checks |
| QEMU/OVMF install, read-only recovery, nondestructive cancel, two disk boots and persisted settings | PASS |
| QEMU USB-only Try, no disk/PS/2, USB HID keyboard/tablet and Home | PASS |
| VirtualBox 7.2.20 Try/Login/Desktop and EFI/AHCI install/disk boot | PASS — automated/headless |
| Actions `reliable-file` [41352989065](https://github.com/KrzysioYT/KuroganeOS/actions/runs/41352989065), CLA [41352989090](https://github.com/KrzysioYT/KuroganeOS/actions/runs/41352989090), ISO [41352989006](https://github.com/KrzysioYT/KuroganeOS/actions/runs/41352989006) | PASS |
| Exact ISO SHA-256 | `0b8b0b197bf0455a7e06939597cb94c769bed331174cb675af3320deced63531` |
| Manual visible VirtualBox and scaled-control hit-testing | UNVERIFIED |

QEMU manifest `/tmp/kuro-ui-scale-exact-qemu-64577459/result.json` records the
exact source SHA, ISO/disk hashes, restored theme `2`, text scale `125` and
`PASS`. Guest coverage has not yet asserted the native-widget hit target at a
non-100% scale. Flux 8 remains open.

## Historical runtime qualification — `7287fe1`

Draft PR #58 currently points to runtime SHA
`7287fe1d4f092e146ceda014ebf0a440c1aa4e5f`. It includes the read-only disk
recovery fix and a global, persistent 100/125/150/200% text-rendering scale.
Settings changes and verifies the value; Home reapplies it at session startup.
The setting does not scale whole-UI geometry or isolate preferences per user.

| Evidence | Result |
| --- | --- |
| Full host regression | PASS — exact source tree |
| Core UBSan | PASS — 11 groups |
| Debug kernel/userspace build | PASS |
| Release installer, SDK and ISO | PASS |
| GitHub Actions `reliable-file` run `41348763286` | PASS |
| GitHub Actions `iso-qualification` run `41348763424` | PASS — host/SDK, IMG/ISO, FAT32/VFS, ISO x20, OVMF and three NAT profiles |
| GitHub Actions CLA run `41349152187` | PASS |
| QEMU/OVMF USB-only setup ISO Try, no PS/2/disk, USB keyboard/tablet | PASS — zero block devices, live Login and Home |
| QEMU/OVMF install and two disk-only boots | PASS — read-only recovery and nondestructive cancellation included |
| QEMU Settings scale write and second-boot restore | PASS — `125%` |
| ISO SHA-256 | `cf1f0c0e7d21ccb3c92368508a1f16096310a3113e08815adaef814d7266be80` |
| VirtualBox 7.2.20 Try/Login/Desktop, headless PS/2, exact ISO | PASS |
| VirtualBox EFI/AHCI install, ISO detach, disk-only boot, FAT32/PID 1 and PCnet NAT/DHCP/gateway/DNS | PASS — fresh temporary VDI |
| Visible/manual VirtualBox mouse and keyboard | UNVERIFIED |

The QEMU manifest records source SHA `7287fe1d4f092e146ceda014ebf0a440c1aa4e5f`,
the ISO hash above, restored theme ID `2`, restored text scale `125`, and
result `PASS`. A separate QEMU USB-only Try boot of the same ISO attached no
system disk, reported zero installer block devices, accepted USB keyboard and
report-pointer input, and reached Home. The older
`ac0d122` and `7c2171a1` evidence below is historical and does not transfer
to this head. PR #58 remains Draft and needs one approval; Flux 8 is not
qualified.

## Exact-SHA checkpoint — 2026-11-01

Runtime commit `ac0d1220046d35b194e2a6a38de8135258232522` is a tested
integration checkpoint, not a release candidate or 8.0 closeout.
Host regression/harness follow-up: `7c2171a1ca5e985334276a955e3ceaf879748d92`
(test and runner changes only; system payload/ISO unchanged).

| Evidence | Result |
| --- | --- |
| Host regression and core UBSan (11 groups) | PASS |
| Host regression Release (`-O2`) and Debug (`-O0 -g3`) | PASS on test commit `7c2171a1` |
| Kernel Debug and Release builds | PASS |
| QEMU/OVMF Try, install cancellation, fresh install and two installed boots | PASS |
| Default account/Home, real Files/Terminal processes, USB window lifecycle and theme reboot persistence | PASS |
| GitHub Actions run `41324279554` | PASS — host, image/FAT32/VFS, ISO x20, OVMF and three NAT NIC profiles |
| GitHub Actions run [41331938452](https://github.com/KrzysioYT/KuroganeOS/actions/runs/41331938452) | PASS — test/harness commit `7c2171a1`; host/ABI/SDK, full regression, IMG/ISO, FAT32/VFS, ISO x20, OVMF and three NAT profiles |
| VirtualBox 7.2.20 Try/Login/Desktop, automated PS/2 input | PASS — headless scripted test |
| VirtualBox EFI/AHCI install, disk boot, FAT32, init and PCnet NAT/DHCP/DNS | PASS — fresh temporary VDI |
| Visible/manual VirtualBox mouse and keyboard | UNVERIFIED — required for acceptance |

The generated ISO is `KuroganeOS-7.0.0-dev-x86_64.iso` with SHA-256
`13d0b1c445aadfbc1a075cec4fb97d4a69487bcae8a6214bf0f2735cc722a8f6`.
QEMU and VirtualBox used fresh scratch disks and the default account without a
password. Password authentication, manual VirtualBox mouse/keyboard use,
physical devices, the old installed-image incident, complete app/error
recovery, HiDPI, licensed scalable typography and the complete 8.0 acceptance
matrix remain open.

## Updated direction — feature-led Flux 8 (2026-10-27)

**Goal:** KuroganeOS 8 should feel like a usable, recognizable, personal
native desktop — not merely a set of compositor checks. Stability is a
mandatory **release property**, not a reason to postpone every independent
user-facing feature until the whole hardware matrix is green.

The Red Flux website screenshot is the visual target. The native desktop must
retain real processes, real files, real settings and honest system telemetry.
Do not use the web simulation or invented status readouts to meet a checklist.

### Red Flux Next direction (2026-10-27)

The refreshed UI concept refines the existing brand, rather than replacing
it: **keep the established KuroganeOS logo** and angular black/graphite/red
identity. Use a narrow navigation rail, legible native window chrome, bottom
running-app switcher, practical Files/Terminal workflows and an appearance
panel. Any design-mockup control must be backed by a real native capability
before it appears as available in the OS. Patterns may be code-drawn; neither
an image-generation mockup nor a web screenshot is guest implementation proof.

### Feature milestones (deliver user value in this order)

| Slice | Concrete deliverables | Proof before marking a feature done |
| --- | --- | --- |
| **8A — Everyday desktop** | Authenticated Home; launch, close, restore, move and switch real applications; visible failures and recovery | Usable Login -> Files and Terminal interaction; owner checks; regression smoke |
| **8B — Red Flux design parity** | Angular graphite/red shell; fixed side rail, bottom taskbar, consistent native app interiors; responsive layouts | Native screenshots and pointer hitboxes compared at 1024×768, 1280×800 and a HiDPI size |
| **8C — Personalization** | Existing six palettes and Modern/Classic; wallpaper/background, accent/contrast, text/UI scaling, reordered/pinned Dock and saved window preferences | Set -> restart/login -> restore; invalid-setting fallback; authenticated session ownership |
| **8D — User workflows** | Discover/search apps, usable Files and Terminal, clipboard/file operations, notification behavior, keyboard-first navigation and accessible errors | Complete real-user tasks without shell-only workarounds or pretend PASS |
| **8E — Developer preview** | Documented external GUI app example, quickstart, native SDK template and readable diagnostics | Build from public SDK -> install/run in guest -> window responds to input |
| **8F — Release qualification** | Exact-SHA regression, media, QEMU USB-only, VirtualBox interactive, install/restore and recovery matrix | Recorded SHA, artifact hash, logs, manual evidence; no critical unresolved failure |

Slices **8B–8E may advance while unrelated 8F evidence is pending**. A failure
that compromises ownership, data integrity or the feature being developed
must still be fixed first. Feature progress is tracked as `PLANNED`,
`IMPLEMENTED`, `HOST-TESTED`, `GUEST-VERIFIED`, then `QUALIFIED`; only the last
is enough to close the milestone. Do not expand 8.0 into the 11.0 package
manager or 12.0 production users/groups, signed update and isolation work.

### User-friendly / developer-friendly acceptance

- A new user can find Home, Files, Terminal, Browser and Settings, understand
  a failed launch, change a theme and revert it without reading a manual.
- The desktop remembers supported appearance choices across a real restart.
  A theme stored globally is **not** proof of a private per-user profile;
  never cross account/session trust boundaries to offer personalization.
- Text remains legible at normal and HiDPI sizes, including Polish characters;
  all essential controls have visible keyboard focus and sufficient contrast.
- A developer can compile a tiny native GUI example from public SDK headers,
  run it in a guest and troubleshoot failure using documented diagnostics.
- New UI features have unit/host coverage as they land and guest interaction
  evidence at integration checkpoints. Preserve Classic and the existing
  keyboard rescue shortcuts during migration.

## Non-negotiable reliability gate (P0)

- Real authenticated Login -> live `RED FLUX HOME` first frame.
- Dock/Home/desktop shortcuts launch a Ring-3 process, not just show startup PASS.
- Click, keyboard, focus, restore, minimize, close, resize and wheel across apps.
- No blank desktop on failed Login/Home: visible retry or PID1-only recovery shell.
- Zero privilege escalation via fake `KUROGANE LOGIN`/`RED FLUX HOME` window titles.
- Exact-SHA release media, QEMU USB-only login and actual Files click, VirtualBox
  interactive smoke, and host optimized/UBSan gates all pass.

## Native compositor and Modern style (P1 implementation checklist)

- [x] Own software backbuffer; preserve clipping, damage and non-XOR cursor.
- [x] Compact floating Dock fallback and Classic retained.
- [x] Square/angular Red Flux Modern chrome, side rail and bottom taskbar
      for 1024×720+ (host-tested 2026-10-27; not VM-qualified).
- [x] Rounded-surface/4x4 corner routines retained in the renderer for
      legacy/optional widgets; bounded label foundation exists. Rounded
      corners are **not** the Modern reference design.
- [ ] Explicit per-client surface lifecycle independent of title strings.
- [ ] Robust focus/capture/z-order and no input event loss under heavy redraw.
- [ ] HiDPI layout factor 100/125/150/200%, fractional scaling without clipped
      widgets, resolution change without login/logout or stale pointer position.
- [ ] Dirty-region and cursor performance telemetry; memory/budget limits.
- [ ] Optional future GPU backend behind same compositor interface.
  **Do not** claim Wayland protocol compatibility without implementing it.

## Typography, accessibility and usability (P1)

- [ ] Complete licensed scalable sans text system with shaping, layout and
      bounded runtime raster/cache budget; the current pre-rasterized atlas is
      an implementation increment, not a complete font service.
- [x] UTF-8 decoding, ASCII/Latin-1, Polish Latin Extended-A glyphs and U+FFFD
      fallback in the native software renderer.
- [x] Antialiased regular/bold atlas at 8, 10, 12, 16, 20, 24 and 32 px;
      text scale selection supports 100/125/150/200% without changing app ABI.
- [x] Fractional 26.6 glyph advances and bounded, generated pair kerning for
      glyphs in the bundled atlas.
- [x] Modern and Classic window titles fit available width using ellipsis at
      UTF-8 code-point boundaries; no title glyph is split.
- [x] Native scene labels word-wrap to their supplied width and height,
      falling back to code-point-safe breaks for long words.
- [ ] General text fit and ellipsis preserve code-point boundaries at every
      scale and in every native app layout.
- [ ] Full Unicode shaping and general app-level ellipsis in proportional
      layout; the current font covers only its documented glyph set and
      provides pair kerning rather than script shaping.
- [ ] Keyboard focus visibility, logical tab order, input edit/caret, mouse wheel.
- [ ] Contrast validated in all six themes. Reduced-motion fallback.

### Native font atlas increment (2026-11-01)

- Added DejaVu Sans regular/bold source fonts under their Bitstream Vera
  license. Provenance, hashes, generator command and the same license notice
  in the root filesystem are recorded in `third_party/fonts/dejavu/README.md`.
- The host-side Pillow generator produces a compact 4-bit antialiased atlas
  for ASCII, Latin-1, Polish Latin Extended-A and U+FFFD at seven sizes. The
  kernel decodes the 260,894-byte atlas once into static memory guarded by a
  512 KiB compile-time ceiling. Glyph advances and generated pair adjustments
  use 26.6 fixed point; the current 5,232 kerning pairs have an 8,192-entry
  compile-time ceiling. Rendering is allocation-free and preserves the
  existing syscall/UI ABI. `draw_char` remains the 5x7 console primitive.
- Host tests exercise proportional advances, Polish and replacement glyphs,
  grayscale coverage, regular/bold differences, clipping and the four text
  scale selections. On the working tree based on `05e67146`, the full host
  regression and core UBSan suite passed; fresh isolated Debug and Release
  kernel builds and deterministic atlas regeneration also passed. These
  results are not GitHub Actions evidence for a published commit.
- User-selectable, globally persisted text scale is now implemented through
  the Settings service and an append-only desktop syscall. Native Ring-3
  command bounds use this scale, and pointer/scroll events are inverse-mapped
  to the apps' logical coordinates. Home reapplies a valid value at session
  startup. This does not yet scale app-icon internals, desktop/window chrome or
  display resolution, and it is not an isolated per-user profile. Host tests
  cover the 100/125/150/200% coordinate transforms; a guest assertion for
  scaled native-widget hit targets is still open.
- This still does not provide text shaping, general Unicode coverage,
  paragraph wrapping/general ellipsis in every proportional-text surface, or
  complete visible desktop qualification. ISO/media packaging and guest visual
  validation for this increment are NOT RUN.

### Native window-title fitting increment (2026-11-01)

- Modern and Classic window titles now use the proportional bold font and
  ellipsize within the available title-bar width. The renderer measures only
  complete UTF-8 code points and does not split a multibyte character.
- Native scene labels now wrap at spaces, hard-wrap long words only between
  decoded UTF-8 code points, and stop at the allocated line height.
- The atlas generator also records fractional glyph advances and pair
  kerning, which the renderer applies consistently to drawing, measurement,
  title fitting and wrapping.
- Host regression coverage verifies the pixel bound, an ellipsis at a valid
  Polish-text boundary, too-narrow widths, word wrapping and bounded lines.
  The repeated-pair regression verifies accumulated kerning. Full host
  regression, core UBSan, deterministic atlas generation and isolated
  Debug/Release kernel builds passed on the working tree based on `561d0a89`;
  ISO and guest visual validation are NOT RUN.

## Desktop application integration (P1)

- [ ] Home, Terminal, Files, Forge Control, Web, Monitor, Settings, About and
      Notifications launch/focus from both mouse and chorded keyboard access.
- [ ] Native app rendering follows Modern styles; Classic can be chosen and
      remains functional across restart.
- [ ] Clipboard, persistent window settings, application registry and file
      dialogs have live integration tests, not just fake labels.
- [ ] Crash/restart ownership cleanup does not leave stale windows or frozen Dock.

## Personalization and product usability (P1 — added 2026-10-27)

- [x] Shared six-theme palette; Modern/Classic selector; typed persistence
      foundation (full restart/session qualification remains open).
- [ ] Wallpaper/background chooser with safe default and user-controlled reset.
- [x] Built-in wallpaper pattern selector (partial 8C): Forged Steel, Graphite
      Grid and Minimal Dark are rendered by the native compositor, selectable
      using three direct radio-like Settings cards and saved under
      `ui.background` with range-checked U64
      values. Home restores the choice after presenting its first frame;
      Reset Interface Profile restores Forged Steel and attempts rollback if
      persistence or live application fails. Individual wallpaper changes
      also confirm the stored value and attempt rollback on a failed live
      update. These are global/session-shared
      settings like the existing theme; private per-account wallpaper, user
      image importing and actual guest restart evidence remain open.
- [ ] Accent/contrast, licensed scalable font and 100/125/150/200% UI scale
      settings consistently applied by compositor **and** native apps.
- [ ] Dock pin/order settings, keyboard shortcuts and sensible defaults.
- [ ] Persist supported window positions/sizes and appearance across restart;
      bound all preferences to the authenticated session when user-scoped.
- [ ] Native launcher search, clear empty/error states and first-run guidance.
- [ ] Real Files/Terminal/Browser/Performance/Settings tasks without simulated
      content; accessibility and keyboard-only acceptance.

### Settings usability increment (8C/8D, 2026-10-27)

- Native Settings has local keyboard focus/activation: Tab/Shift+Tab, arrow
  keys, PageUp/PageDown, Enter/Space. Focus automatically follows controls
  across the bounded settings viewport without bringing back desktop-global
  launch shortcuts.
- Offline service and unavailable audio controls are visibly disabled. Actions
  display save/failure feedback rather than relying on serial markers alone.
- **Reset Interface Profile** now restores default color, Modern style and
  normal contrast, verifies stored values and applies the live compositor
  settings. Failed steps attempt rollback and report a partial failure if the
  previous state could not be restored.
- C11/C++17 host tests cover local keyboard/focus policy; live guest/session
  persistence and reset recovery remain to be qualified before 8.0 stable.

### Terminal workflow increment (8D, 2026-10-27)

- Native Terminal and console shell now expose Ring-3 VFS `ls` to list real
  directory entries and validate `cd` destinations before changing cwd.
- Graphical Terminal supports Tab completion for built-in command names and a
  bounded 96-line scrollback navigated with wheel/PageUp/PageDown.
- Tab now also completes real VFS paths for `cd`, `ls`, `cat`, `read`, `run`,
  `open`, and `which`, using current-directory or `/apps/` lookup as the
  actual command requires. Unique directories gain `/` and files gain a space;
  ambiguous names extend only their common prefix. Quoted/escaped filenames
  and multi-argument completion remain out of scope for this parser.
- C11/C++17 host tests cover completion, scroll bounds, and VFS success/error
  behavior, including path completion. Guest mouse/keyboard verification
  remains open; this is **not** an 8.0 release qualification claim.

### Files workflow increment (8D, 2026-10-27)

- Files has keyboard-first live filename filtering in the current VFS directory;
  typing narrows the results, Backspace edits and Escape clears the filter.
- The catalog keeps up to 512 matching entries (up from 24) in global BSS,
  not on the 32-KiB Ring-3 stack, and reports an explicit truncation limit.
- Native sorting, folder activation, keyboard paging, wheel paging, path copy
  and non-ELF previews remain available on filtered results. Host C/C++ tests
  cover search editing, ASCII-case-insensitive matching and empty states;
  guest usability qualification remains open.
- Files now has session-local Back and Forward controls (also Alt+Left and
  Alt+Right) over the most recent 16 visited directories. Search terms are
  restored on history navigation; entering a new directory after Back removes
  the stale forward branch. A failed VFS open does not advance history or leave
  the user in an invalid folder. Host C/C++ tests cover bounded history,
  search restoration, branch pruning and cursor boundaries.
- Files supports explicit **New Folder** creation through a focused native
  name field and Enter/ESC confirmation. Names are bounded ASCII path segments,
  never interpreted as paths; the operation uses the real Ring-3 mkdir syscall,
  reports read-only/duplicate errors and refreshes the current folder only
  after success. An explicit sync is attempted; an unverified sync is reported,
  not silently declared durable. Host C/C++ tests cover name validation,
  editing, cancellation, submission and length boundaries. Writable KuroFS
  guest interaction still needs confirmation.
- Files now has explicit **Rename Selected** for ordinary files and folders,
  with the existing bounded editor, Enter to save and Escape to cancel.
  The UI rejects device/mount-point renames, path traversal, unchanged names,
  existing destinations and unsuccessful destination checks before invoking
  the real Ring-3 rename syscall. On success it attempts a sync, reloads the
  current directory and highlights the new name when present; a filtered view
  is cleared so the renamed item is not hidden. The native window is taller
  to keep all controls visible. Secondary-click selects a folder without
  opening it; primary-click still opens it directly. Long names in the editor
  keep their trailing characters and caret visible. C11/C++17 tests cover
  unsafe names, type restrictions and bounds. The writable-guest rename
  interaction is still unverified.

## Native developer experience (P2 — 8.0 preview, not 11.0 completion)

- [x] Public ELF64 SDK headers, GUI/service templates and sample build
      infrastructure exist; independent guest usability is not yet qualified.
- [ ] Short "build your first GUI app" path verified in a clean guest image.
- [ ] Discoverable process/application log and failure diagnostics.
- [ ] Stable enough documented *8.0 preview* UI usage without promising
      dynamic loading, packages, third-party driver ABI or POSIX compatibility.

## Release gate / acceptance

Do not label 8.0.0 qualified just because themes, wallpaper or Dock render.
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
- [x] Scoped exact-SHA host/UBSan, kernel Debug/Release and QEMU/OVMF checks
      passed at `ac0d1220046d`; see the evidence table above.
- [ ] Full 8.0 acceptance and interactive VirtualBox first-frame test remain
      open. Automated VirtualBox 7.2.20 Try and install/disk-boot tests passed
      on exact runtime SHA `7287fe1`; visible/manual mouse and keyboard
      interaction remains UNVERIFIED.

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

## 2026-10-25 — Try boot and no-target recovery increment

The current working-tree changes fix two reproduced startup blockers without
weakening Login authorization: normal PID 1 startup now has a compositor before
launching Login, and setup-media Try can boot without a target disk. If Install
is selected with no target, the UI gives an explicit explanation and only
enters the read-only live session after user input. Foundation IMG publication
now validates the actual FAT32/VFS payload and all 44 required runtime ELF64
paths, rather than trusting a staging file list.

Working-tree checks on arm64 macOS passed: the optimized host suite under
Rosetta, targeted root-volume UBSan, setup ISO Try/no-disk recovery and a USB
keyboard/tablet Login -> Home run under QEMU/TCG. A separate live ISO run opened
Terminal from the Dock, confirmed a Ring-3 process and presented surface, and
captured a QMP screenshot. Earlier Foundation IMG evidence includes Files and
Terminal launches plus wheel input. These runs precede a source commit and are
not exact-SHA media qualification.

Two later Foundation IMG attempts stopped after `userspace ABI probe passed`
before the Ring-3 preemption marker, so the intermittent boot issue remains
open. Setup/Login/Home/Files/Terminal screenshots were visually inspected;
the boot frame is firmware/loader output, not a graphical KuroganeOS splash.
Full typography/scaling work, app and window lifecycle/recovery, persistent
settings, blank-disk installation, VirtualBox acceptance and the remaining
contract items are still unqualified. Status remains **ACTIVE, NOT QUALIFIED**.
[Current status](../../DEVELOPMENT_STATE.md) takes precedence over dated
working-slice notes. No version promotion is implied.

## Exact-SHA verification — 4ca69f57

Source SHA: `4ca69f5797904f4152ad4a74ef3b8ceb3d807847`. This is a verified
runtime increment, not a qualification of the contract above:

- On arm64 macOS, the release IMG and ISO built in an isolated worktree for
  QEMU/TCG. The resulting root FAT32/VFS image contains all 44 required
  ELF64 programs; the root-image validator and targeted UBSan test passed.
- The optimized host suite passed under Rosetta, including the new live
  package VFS directory enumeration regression.
- USB keyboard/tablet QEMU runs with PS/2 disabled verified Try → Login →
  authenticated Home, Files directory enumeration and wheel input, and a
  Terminal Ring-3 process with a presented surface. The exact run required
  two child spawns and two compositor-observed surfaces.
- A separate exact-SHA ISO run selected Install without a target disk and
  verified the explicit refusal, read-only live package, authenticated
  session and first Home frame.
- Guest screenshots for setup refusal, Login, Home, Files and Terminal were
  captured and visually reviewed. They still use transitional bitmap text;
  no graphical OS splash was evidenced.

Media SHA256:

| Artifact | SHA256 |
| --- | --- |
| `KuroganeOS-7.0.0-dev-macos-qemu.img` | `48723c4f84de9d28897cafc4fb5bf2862d435b0a8112855de64de86ad0a856cf` |
| `KuroganeOS-7.0.0-dev-x86_64.iso` | `53586c4f5fc98a8a983c9ab36d439de3ab41bbdd56740deb15c58930b674cd14` |

Still open: VirtualBox manual acceptance, installation and boot from a blank
test disk, full app/window/focus/input/clipboard and recovery matrix,
settings persistence after restart, licensed scalable font rendering,
HiDPI/responsive layout, and remaining contract checks. The exact-SHA
successes above do not establish those behaviors or qualify 8.0.

## 2026-10-27 — Automated live window lifecycle slice

The QEMU gate now has an additional USB-only `--flux-lifecycle` scenario for
Terminal/Files. It checks completed guest state transitions rather than only
startup markers: minimize/restore (including maximized restore), move, resize,
focus, close, new PID/window-generation relaunch and repeated resource cleanup.
The first local working-tree ISO run passed the single-cycle lifecycle on
macOS QEMU/TCG. The expanded churn gate is separately recorded with its tested
source and logs; no full 8.0 completion or VirtualBox equivalence is implied.

## 2026-10-27 — Settings keyboard accessibility slice

Settings exposes its existing native controls through Tab/Shift+Tab and
Enter/Space, retaining visible focus after actions and scrolling to off-screen
controls. Mouse interaction remains available. Production-policy host tests and
the QEMU lifecycle gate cover navigation; the installed-disk gate separately
checks actual theme activation and service readback on a writable root.
This is one application accessibility increment, not full desktop acceptance.

## 2026-10-27 — Dock identity slice (not an 8.0 closeout)

The Dock's running/focus/readiness lookup now binds a window's canonical title
to the creating Ring-3 process's installed executable path. The result is
cached per generation-safe window slot, so spoofed titles from other session
apps cannot satisfy a pinned application's readiness/focus check. This is a
bounded piece of client identity, **not** the complete per-client surface
lifecycle, authentication, recovery or compositor acceptance gate above.
Host regression passed on the working candidate; guest/VirtualBox gates and
the exact-SHA qualification remain outstanding.

## 2026-10-27 — Home instant app search (8D increment)

Home now offers a real native search field over the installed first-party app
catalog. Physical typing filters app tiles by title, description and executable
name; Backspace/Escape edit or clear, arrows/Tab change the matching selection,
and Enter launches/focuses through the existing session-owned app lifecycle.
No-match state is explicit, and the Home view accepts wheel scrolling while
preserving the private Dock app-command transport. The bounded ASCII search
helper has host coverage, and the Ring-3 launcher builds with the public SDK.
Full UTF-8 input, accessibility review and QEMU/VirtualBox interaction remain
open; this is an implemented 8D feature, not a qualified 8.0 milestone.

## 2026-10-27 — Cross-PR integration checkpoint

The candidate combines PR #56 lifecycle/installed-theme evidence with PR #57
native Home search, Files and Terminal workflows, persistent wallpaper presets
and Dock executable-identity checks. Settings retains Tab/Shift+Tab activation
and QMP markers from the lifecycle gate, plus arrow/Page navigation and the
profile reset behavior. The saved-theme serial value is emitted only after a
verified successful SET/GET and live theme application.

At the 2026-10-27 checkpoint, the combined source passed `make test` and
`make kernel` on Linux x86, while media packaging was **BLOCKED** by unavailable
`sgdisk`. The later exact-SHA checkpoint above built and booted release media.
VirtualBox, font and HiDPI gates remain open. The runtime stays `7.0.0-dev`.

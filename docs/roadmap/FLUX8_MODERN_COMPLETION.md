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

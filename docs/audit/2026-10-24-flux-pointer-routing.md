# Flux Desktop 8.0 — pointer ownership, capture and chrome isolation

> Zapis historyczny / dowód dla wskazanej rewizji. Nie jest instrukcją
> bieżącego wydania. Aktualny stan: [DEVELOPMENT_STATE.md](../../DEVELOPMENT_STATE.md).

Date: 2026-10-24. This is a scoped Window Manager 2.0 engineering candidate.

## Correctness defect

Before this change, `windowing::dispatch` delivered every pointer event
to `g_focused`, regardless of pointer position. Hover/wheel could therefore
reach the wrong application's IPC event queue. System-drawn chrome also had
paths that leaked pointer events to application callbacks.

## Contract

- Keyboard events remain focus-directed.
- Mouse movement, button events and wheel use topmost visible client-content
  hit testing. Moving over a nonfocused app never implicitly gives it focus.
- A left press in client content focuses and captures that app; move and
  left release stay with it outside the window until release.
- Window chrome, desktop dock/shortcuts and drag/resize consume their own
  pointer gestures; they cannot generate application clicks or releases.
- Close/minimize/resize/initialize clear stale pointer capture. Generation-safe
  `WindowId` resolution prevents sending events to slot replacements.
- Callback may close its own window. Dispatch does not use the closed slot
  after invoking the callback.

## Regression and qualification

`test_window_manager.cpp` adds multi-window hover, wheel and keyboard
targeting, external gesture capture, chrome drag isolation, close-during-
capture and close-from-input-handler cases. The existing Flux foundation
exact-SHA gate builds optimized and UBSan host regressions and runs the
OVMF/QEMU USB-only Ring-3 desktop login. This is a foundation milestone,
not complete 8.0.0 and not physical hardware qualification.

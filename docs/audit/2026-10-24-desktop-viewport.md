# Desktop viewport transitions — 2026-10-24

> Zapis historyczny / dowód dla wskazanej rewizji. Nie jest instrukcją
> bieżącego wydania. Aktualny stan: [DEVELOPMENT_STATE.md](../../DEVELOPMENT_STATE.md).

This change adds the compositor/input portion of dynamic display sizing for the
8.0 roadmap. It does not implement hardware mode switching or qualify 8.0 stable.

`windowing::set_screen_size` preserves window IDs, process ownership, z-order,
focus and retained payloads. It fits normal and restore bounds into the new
workspace, refits maximized windows, resizes the login surface and cancels stale
drag/resize/hover capture. Invalid dimensions and calls during painting fail
without modifying the window layout. Identical dimensions do not schedule work.
The compositor notices a changed, already configured framebuffer before rendering
and applies the new size. The backend must install a valid mapped framebuffer
outside an active render; no UEFI runtime mode-setting call is introduced.

`input::set_screen_bounds` clamps pointer and queued event coordinates while
retaining queue order, button/key transitions, held buttons, counters and pending
driver samples. These APIs use the existing serialized polling/compositor model;
they do not add cross-CPU synchronization or a userspace mode-setting syscall.

Minimizing a maximized window now remembers its previous state. Restoring it
returns to the maximized workspace; a second restore returns to its normal bounds.
This also works when the viewport changes while the window is minimized.

Host regression covers shrink/grow, retained data/identity/focus, small viewport
clipping, invalid dimensions, rejection during painting, no-op resize, maximized
restore and queued input/button preservation. Optimized and UBSan tests pass.
The release kernel builds. The focused Flux CI also runs input tests and UBSan.
Actual display hardware mode changes remain an unqualified backend requirement.

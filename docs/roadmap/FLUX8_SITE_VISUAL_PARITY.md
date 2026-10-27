# Flux 8 Modern — Red Flux web-reference parity

Status: **visual parity in progress**, not a release qualification.

Reference: the KuroganeOS Red Flux desktop demo screenshot supplied on
2026-10-27, backed by the existing Site's `DesktopDemo.tsx` and `.rf-*` CSS.
The demo is a design reference; simulated web content is not an OS capability.

## Landed in the native compositor

- For Modern sessions at **1024×720 or larger**, the desktop uses a fixed
  graphite top identity rail, a permanent left navigation dock and a full-width
  bottom taskbar. The screen's work area reserves their actual geometry so
  maximized windows do not disappear beneath the controls.
- Navigation groups match the demo's labels/order: **Home, Vault, System,
  Terminal, Docs, Tools**. They route respectively to the actual Home, Files,
  Settings, Terminal, Browser and Performance apps. Other app IDs remain
  available through Home and Ctrl+Alt+F1…F8, without changing the app ABI.
- Bottom tabs track real windows and use existing focus/restore behavior.
  The bottom Home mark focuses the session launcher.
- Modern surfaces use square one-pixel borders, graphite fills, restrained
  red focus signals and angular resize grips. Window controls preserve their
  minimize/maximize/close semantics; titles are bounded before control space.
- The header displays the **compiled native version**, not the web demo's
  simulated `3.3.3`. An online session indicator depends on Home existing.
- For compact displays and the Classic style, the previous floating dock
  remains reachable. This is a compatibility fallback, not a second set of
  active desktop controls.

## Still required before any "1:1" claim

1. Boot the exact native commit in **QEMU and VirtualBox** at reference-size
   and compact resolutions; capture frames and compare text, panels, app
   spacing, wallpaper, iconography and pointer hitboxes against the web demo.
2. Bring native Files, Terminal, Browser/Docs and Performance *content* to
   the same density and layout. Their current Ring-3 implementations are
   functional applications, not copies of the simulated web panes.
3. Qualify font metrics, glyph/icon assets, DPI scaling, multiple themes,
   keyboard navigation, taskbar overflow and accessible focus states.
4. Add trustworthy hardware-backed clock/status indicators with an idle
   refresh policy; avoid fake connectivity, battery or performance readouts.
5. Retest dragging, resize, maximize, suspend/restore and session recovery
   in real virtualized environments. Host geometry tests alone are not a
   native screenshot or stable-release gate.

Version policy: neither Flux 8.0.0 stable nor KuroganeOS 15.0.0 stable is
claimed by this visual patch. The compiled version remains authoritative.

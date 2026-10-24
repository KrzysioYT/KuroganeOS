# KuroganeOS Flux 8 — 2026-10-24 integration & qualification

## Scope and evidence
This is development **8.0**, not release 8.0.0-dev or stable.
The latest integration branch must pass its exact SHA gates.

Run local verification on Windows 11 + Ubuntu WSL2:

```powershell
cd D:\\KuroganeOS
git switch codex/road-to-15-stability
git pull --ff-only origin codex/road-to-15-stability
wsl -d Ubuntu
```

```bash
cd /mnt/d/KuroganeOS
bash scripts/run-host-tests.sh
bash scripts/build-media-linux.sh --configuration release --rebuild
bash scripts/smoke-uefi-iso-qemu.sh \
  build/images/KuroganeOS-linux.img --disk --timeout 240 \
  --no-ps2 --usb-keyboard --usb-tablet \
  --log-dir build/tests/flux8-manual
```

The last command is **headless**. For graphical exploration use a
visible `qemu-system-x86_64` GTK or SDL instance with a writable OVMF
VARS copy, the built `KuroganeOS-linux.img` in `snapshot=on` mode,
`qemu-xhci`, `usb-kbd` and `usb-tablet` on the same controller.

## Manual 8.0 UX acceptance
- Log in, open Settings by mouse.
- Cycle all 6 palettes, close and reopen Settings; inspect Home, window
  chrome, Dock, backgrounds and colors of applications already open.
- Switch Modern ↔ Classic and verify both modes.
- Scroll Settings to reach volume and reset controls; repeat with fast
  multi-detent wheel input.
- In Files select a file, click COPY SELECTED PATH and open Kurogane Web
  to paste into the omnibox.
- In Kurogane Web COPY ADDRESS, edit the field, then PASTE ADDRESS and
  verify original contents returned from the real `clipboard.v1` service.
- Boot the test image *persistently* on a disposable disk copy; check
  saved theme/style survive a clean restart (snapshot mode alone cannot
  test persistence).
- Inspect CPU (scheduler estimate), RAM (PMM used frames), disk (activity
  index), GPU (software draw index), and network stack state labels.
- Exercise moving/resizing/multiple windows, Alt+Tab and close/focus,
  then reboot and repeat.

## Known blockers
A modern production font renderer, switchable GOP display modes and
resolution-aware layout, drag-and-drop, and complete clipboard editing
remain unimplemented. Pass logs should never claim those features.

## Responsive desktop window geometry (2026-10-24 candidate)

Public `KU_SYS_DISPLAY_INFO = 70` exposes actual GOP framebuffer width/height
and the compositor's current usable work area through a strict 32-byte ABI,
including version/size and user-pointer validation. Native Ring-3 `gui_open`
fits requested window sizes and positions to this area without changing normal
1024x768 layouts. Pure C11/C++17 host regressions check 800x600, 1024x768,
1920x1080, inverted/invalid dimensions, small workspaces and integer extremes.

**Not** runtime resolution switching: GOP cannot be arbitrarily reprogrammed
after ExitBootServices; mode-setting driver and resizable layout for individual
widgets still require separate qualification. This improves initial window
placement only.

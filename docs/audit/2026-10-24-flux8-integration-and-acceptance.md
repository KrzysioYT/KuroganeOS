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

## Scoped keyboard clipboard integration (2026-10-24)

The Ring-3 UI event retains its 32-byte ABI while assigning the previously
reserved final word to Shift/Ctrl/Alt modifier flags **for key events only**;
scroll events continue to use the same union member as signed `wheel_y`.
Kurogane Web now supports Ctrl+A/C/X/V in its focused omnibox, including
bounded append/selection replacement and no accidental Ctrl-key text entry.
Clipboard contents are served by production `clipboard.v1`; other apps
remain mouse-first and must not see this as general clipboard completion.

The keyboard driver translates Ctrl+C to ASCII 0x03 (not 'c'). An additional
bounded `ku_ui_clipboard_shortcut()` helper matches the physical KeyCode A/C/X/V
with Control in the 32-byte Ring-3 ABI. Native hosted C/C++ tests assert the
real control-character behavior, non-control typing and wheel union exclusion.

## One-command WSLg visual testing

After pulling the integration branch in `D:\\KuroganeOS` and entering Ubuntu
at `/mnt/d/KuroganeOS`:

```bash
bash scripts/run-flux8-wsl.sh --build
```

This compiles release media, opens a real GTK QEMU window with OVMF, q35,
xHCI USB keyboard + **USB Tablet** (the pointer profile verified in the
user's WSLg environment), no PS/2, a serial log at
`build/tests/flux8-manual/serial.log`, and a **snapshot-on** disk.

To test persistence of `ui.theme` and `ui.style` across guest reboots:

```bash
bash scripts/run-flux8-wsl.sh --persistent
```

The persistent flag writes **only** to an independent
`build/tests/flux8-manual/flux8-persistent.img` copy and reuses it between
runs. It does not modify the build image, host Windows/Arch disks, or source.
No VM run constitutes acceptance without checking logs and interaction.

## Flux Files keyboard navigation — follow-up candidate

Files Explorer routes Ring-3 KEY events via a bounded, testable policy. Arrows,
Home/End and Page Up/Down select visible entries; Enter activates,
Backspace/Escape goes to parent, Ctrl+C copies the selected path via
clipboard.v1. Ctrl/Alt navigation does not override WM shortcuts. The app
prints `flux_files_keyboard_navigation: PASS` only after a real key selection.
C11/C++17 optimized and UBSan host regression covers bounds, empty pages,
modifiers and physical Ctrl+C with ASCII ETX. QEMU live input qualification
on the new exact SHA remains outstanding; no release version change.

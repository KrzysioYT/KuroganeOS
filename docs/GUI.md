# Graphics, input and Kurogane Flux Desktop

KuroganeOS 2.4 uses the UEFI GOP framebuffer with software rendering. There is
no accelerated GPU driver yet. PS/2 keyboard/mouse and supported xHCI HID input
feed the shared InputManager.

## Desktop boot model

Normal userspace boot starts the Flux desktop session introduced in 2.3. Safe
Mode remains the text-only emergency environment.

The normal path is:

```text
UEFI
 -> kernel
 -> persistent FAT32 root
 -> scheduler/input
 -> Flux desktop session
 -> /system/init (PID 1)
 -> /gui/terminal
 -> /gui/files
 -> /gui/sysmon
 -> /gui/settings
 -> /gui/about
```

PID1 supervises those Ring-3 desktop applications. A child that exits during a
normal session is restarted. If most GUI children die immediately during the
initial session probe, PID1 falls back to `/apps/shell` rather than spinning in
a broken GUI respawn loop.

Runtime evidence for a healthy desktop includes:

```text
[TEST] desktop_session: PASS
[TEST] userspace_init_spawn: PASS
[TEST] desktop_userspace_apps: PASS
[TEST] userspace_desktop_session: PASS
```

## 2.4 Flux Window Core

The WindowManager supports twelve generation-checked slots containing bounds,
restore bounds, owner PID, normal/minimized/maximized state, z-order, focus,
title and callbacks. It implements hit testing, focus raise, header drag, close,
minimize, maximize/restore, Alt+Tab, Alt+F4 and a software cursor.

2.4 removes the classic desktop chrome from the main WindowManager path:

- no conventional full-width taskbar;
- no textual `-`, `[]`, `X` controls;
- dynamic `Signal Spine` shows real window order/focus;
- floating `Pulse Ribbon` represents active/minimized surfaces;
- Pulse Ribbon items restore or focus windows;
- Flux control rail uses geometric minimize/expand/dismiss controls;
- focused/background surfaces receive distinct signal treatment;
- maximize uses an explicit Flux `work_area`;
- dragging is clamped to that work area;
- a shared bottom-right resize-grip geometry is already exposed for future
  interactive resize.

`WorkspaceGeometry` and `ChromeGeometry` are the single geometry source for
rendering, hit testing and hosted tests. This removes the old duplicated magic
numbers for taskbar height and textual control positions.

Rendering is still immediate-mode and full-frame when invalidated. Damage
tracking/backbuffers are planned for the compositor stages later in the roadmap.

## Public ABI and libui

Syscalls 14-17 currently provide create, present, poll and close. Each process
owns one live window. The kernel copies a fixed `ku_ui_frame` and delivers key,
pointer and close events through a bounded per-process queue.

This API is intentionally still small. 2.5 will evolve it toward a real Flux UI
runtime with views/widgets, layouts, scrolling, inputs, dialogs and dirty
regions instead of treating an application as a fixed list of text lines.

## Ring-3 applications

The PID1 desktop set is:

| Application | Path | Current role |
|---|---|---|
| Flux Terminal | `/gui/terminal` | commands, app launch and job tracking |
| Files | `/gui/files` | persistent-root file view |
| System Monitor | `/gui/sysmon` | live process/system information |
| Settings | `/gui/settings` | session visual settings preview |
| About | `/gui/about` | version/platform information |

The old kernel Monitor/Files/About surfaces remain diagnostic legacy code and
are not the primary desktop session. Their old `ui::taskbar()` compatibility
helper remains temporarily and is scheduled for removal from those Ring-0
surfaces during the remaining 2.4.x cleanup.

## Known GUI limitations

- software framebuffer rendering only;
- no general interactive resize yet (geometry is reserved in 2.4);
- one live UI window per process in the public ABI;
- fixed-frame `libui` model rather than a widget tree;
- no clipboard, Unicode text input or accessibility layer yet;
- no multi-monitor or GPU compositor;
- legacy bootloader strings and a few diagnostic Ring-0 surfaces still need
  cleanup in later 2.4.x patches.

See `docs/roadmap/DESKTOP_ROADMAP.md` for the 2.3 → 3.6 plan.

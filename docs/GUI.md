# Graphics, input and Kurogane Flux Desktop

KuroganeOS 2.3 uses the UEFI GOP framebuffer with software rendering. There is
no accelerated GPU driver yet. PS/2 keyboard/mouse and supported xHCI HID input
feed the shared InputManager.

## 2.3 desktop boot model

Normal userspace boot no longer depends on manually selecting the old
`boot=desktop` experiment. When `user::console` becomes active, the kernel
resolves the 2.3 desktop-session hook and starts the `flux-session` application.
Safe Mode does not initialize `user::console`, therefore it remains a text-only
emergency environment.

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

Runtime evidence for a healthy 2.3 desktop includes:

```text
[TEST] desktop_session: PASS
[TEST] userspace_init_spawn: PASS
[TEST] desktop_userspace_apps: PASS
[TEST] userspace_desktop_session: PASS
```

## WindowManager

The current WindowManager supports twelve generation-checked slots containing
bounds, restore bounds, owner PID, normal/minimized/maximized state, z-order,
focus, title and callbacks. It implements hit testing, focus raise, title drag,
close, minimize, maximize/restore, Alt+Tab, Alt+F4 and a software cursor.

Rendering is still immediate-mode and full-frame when invalidated. The current
WindowManager presentation retains some legacy Desktop Alpha conventions such
as a taskbar-like region and classic controls. Removing those remaining legacy
conventions is the explicit goal of **2.4 Flux Window Core**.

## Public ABI and libui

Syscalls 14-17 currently provide create, present, poll and close. Each process
owns one live window. The kernel copies a fixed `ku_ui_frame` and delivers key,
pointer and close events through a bounded per-process queue.

This API is intentionally still small. 2.5 will evolve it toward a real Flux UI
runtime with views/widgets, layouts, scrolling, inputs, dialogs and dirty
regions instead of treating an application as a fixed list of text lines.

## Ring-3 applications

The 2.3 PID1 desktop set is:

| Application | Path | Current role |
|---|---|---|
| Flux Terminal | `/gui/terminal` | commands, app launch and job tracking |
| Files | `/gui/files` | persistent-root file view |
| System Monitor | `/gui/sysmon` | live process/system information |
| Settings | `/gui/settings` | session visual settings preview |
| About | `/gui/about` | version/platform information |

The old kernel Monitor/Files/About surfaces remain diagnostic legacy code and
are not the primary 2.3 desktop session.

## Known GUI limitations

- software framebuffer rendering only;
- no general window resize yet;
- one live UI window per process in the public ABI;
- fixed-frame `libui` model rather than a widget tree;
- no clipboard, Unicode text input or accessibility layer yet;
- no multi-monitor or GPU compositor;
- some WindowManager chrome is still legacy and is scheduled for replacement
  in 2.4.

See `docs/roadmap/DESKTOP_ROADMAP.md` for the 2.3 → 3.6 plan.

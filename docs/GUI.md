# Graphics, input and Desktop Alpha

UEFI supplies a GOP framebuffer. The kernel renders pixels, bitmap text,
controls and cursor in software; there is no accelerated GPU driver.

PS/2 keyboard/mouse and the xHCI USB HID boot keyboard feed a unified bounded
InputManager. Mouse packets become clamped absolute coordinates, buttons and
wheel. Consumers receive copied events, never driver-owned buffers.

## WindowManager

Twelve generation-checked slots carry bounds, restore bounds, owner PID,
normal/minimized/maximized state, z-order, focus, title and callbacks. The
manager implements hit testing, focus raise, title dragging, close, minimize,
maximize/restore, taskbar restore/focus, Alt+Tab, Alt+F4 and software cursor.
Rendering is an immediate full redraw of visible windows when dirty.

## Public ABI and libui

Syscalls 14-17 create, present, poll and close. Each process owns one live
window. The kernel copies an exact 800-byte frame with up to 12 fixed lines and
an optional progress value. Key, pointer and close events are copied into a
16-entry process queue; overflow discards the oldest event safely. `libui`
initializes structures/colors, copies bounded lines and normalizes polling.

## Ring 3 applications

Desktop mode starts five simultaneous ELF64 processes:

| Application | Real behavior |
|---|---|
| Terminal | accepts `help`, `pid`, `about`, `clear` via public ABI |
| Files | reads real `/etc/system.cfg` through libc/VFS |
| System Monitor | shows live PID/TID and scheduler heartbeat |
| About | reports current 2.0 architecture |
| Settings | toggles actual per-session frame colors |

The legacy kernel Monitor/Files windows remain diagnostic/launcher surfaces.
With a system window focused, `T`, `X`, `U`, `I`, `S` launch apps and `Q` closes
that window. A key focused on a user window is delivered only to its process.

Hosted tests cover focus, z-order, generation, controls and routing. QEMU
injects actual PS/2 motion/button/key events and requires multiwindow, drag,
close and all five userspace app markers.

Limitations include no GPU composition, resize, Unicode input, clipboard,
accessibility, persistent global theme, desktop associations or multi-monitor.
Settings is intentionally small and has no placeholder pages.

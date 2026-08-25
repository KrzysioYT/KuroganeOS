# KuroganeOS 5.0 — Pixel UI contract

KuroganeOS 5.0 intentionally keeps the compact bitmap/pixel desktop instead of
trying to reproduce a modern Windows/macOS-style compositor. The UI is a system
interface, not the main development target.

The visual direction is now **frozen** unless a change fixes usability,
correctness, accessibility or performance.

## Pixel font family

Kernel/UI rendering exposes three deterministic bitmap faces:

- **UI Pixel** — proportional 5x7 face for menus, labels and applications;
- **Mono Pixel** — fixed-width face for Kurosh, code and diagnostics;
- **Display Pixel** — bold all-caps pixel face for application and section titles.

No TTF/OpenType/FreeType dependency is required for the system UI. Browser page
fonts remain a separate future concern and must not dictate kernel UI design.

## Desktop applications

The current application set remains:

- Blade Launcher;
- Kurosh;
- Vault;
- Anvil;
- Forge Control;
- Pulse;
- Kurogane Web;
- Performance;
- System Monitor;
- About.

Existing Ring-3 UI ABI v2, icon registry, pointer/keyboard input and WindowManager
behavior stay supported. Do not add a new UI ABI only for decorative layout.

## UI work still allowed

Only practical work remains in scope:

- fix clipping, broken hit testing or unreadable text;
- improve cursor/input latency;
- reduce unnecessary redraws and framebuffer traffic;
- keep icons and colors internally consistent;
- fix application launch/focus behavior;
- keep browser chrome usable while the web renderer evolves.

Large visual rewrites, spatial layout experiments and attempts to match a design
mockup 1:1 are out of scope for 5.0.

## KuroganeOS 5 development focus

After the pixel UI freeze, engineering priority moves to platform capability:

1. QEMU/macOS functional qualification.
2. E1000 + DHCP + DNS + TCP reliability.
3. HTTP and HTTPS/TLS reliability against real Internet targets.
4. Kurogane Web HTML/CSS parsing, layout, paint and navigation.
5. Framebuffer and graphics correctness, including image/2D rendering.
6. Kurogane Graphics and D3D compatibility work where real backend support exists.
7. Storage, VFS, package management and application lifecycle reliability.
8. QEMU, VirtualBox and eventually physical-PC qualification.

macOS on Apple Silicon runs the x86-64 guest through QEMU TCG, so it is used for
**functional correctness**, not compositor FPS benchmarking.

## 5.0 rule

The public version becomes `5.0.0` when platform functionality and release
qualification are ready. Pixel UI does not need another redesign before that
release.

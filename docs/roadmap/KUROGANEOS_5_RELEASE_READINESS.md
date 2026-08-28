# KuroganeOS 5.0 release readiness

This is the release gate for changing the public version from `3.3.3-dev` to
`5.0.0`.

The system UI is now intentionally frozen as a compact pixel/bitmap interface.
A 1:1 visual recreation of the old Forged Steel mockup is **not** a release
requirement.

## Already real

- x86-64 UEFI boot;
- Foundation GPT image and VFS;
- PID 1 and isolated Ring-3 ELF applications;
- WindowManager, native UI ABI v2, icons and mouse/keyboard input;
- three pixel font faces for UI, terminal and display headings;
- E1000/PCnet/VirtIO network qualification paths;
- DHCP, DNS, TCP, HTTP and HTTPS/TLS stack;
- Kurogane Web shell with URL parsing, history, redirects and link activation;
- HTML text/title/link extraction and basic page rendering;
- audio AC97 path;
- QEMU/OVMF and VirtualBox qualification infrastructure.

## 5.0 blockers

The release must not be tagged until these are qualified:

1. public DNS and strict HTTPS for docs/repo/downloads;
2. macOS QEMU functional run on the current release image;
3. Kurogane Web completes a real public TLS page load;
4. richer HTML/CSS layout and stable framebuffer paint;
5. image/2D graphics rendering does not corrupt neighboring windows or memory;
6. Ring-3 application crashes cannot kill the shell;
7. filesystem write/read/sync survives reboot where media is writable;
8. VirtualBox reference profile passes;
9. Linux, Windows and macOS build/media scripts produce consistent artifacts;
10. wider physical-hardware qualification.

## Qualified in current QEMU pipeline

- ISO and IMG production plus 20 ISO structure checks;
- OVMF boot through login and the Forged Steel desktop;
- application/window lifecycle host coverage including the permanent Blade rail;
- DHCP, DNS, TCP and HTTP against QEMU user NAT;
- separate E1000, PCnet and VirtIO-net qualification paths;
- source-controlled Mozilla CA bundle validation and fail-closed TLS setup.

## Performance policy

Apple Silicon macOS runs the x86-64 guest through QEMU TCG. Treat that platform
as a correctness test, not an FPS benchmark. Performance regressions should be
measured on accelerated QEMU/WHPX or later physical hardware.

## Deferred beyond the release gate

These are useful but do not justify delaying 5.0 by themselves:

- scalable TTF/OpenType system fonts;
- modern translucent/composited desktop effects;
- 1:1 mockup reproduction;
- Wi-Fi/Bluetooth/battery UI without corresponding real services;
- Chromium/Blink compatibility;
- complete Direct3D feature-level parity.

Do not advertise unsupported platform capabilities merely to make the UI look
more complete.

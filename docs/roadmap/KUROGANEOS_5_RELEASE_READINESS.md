# KuroganeOS 5.0 release readiness

This file is the release gate for changing the public version from the current
`3.3.3-dev` development line to `5.0.0`.

The canonical visual reference is `Forged_Steel_GUI_Reference.png`. A passing
build alone is not sufficient to call the system 5.0.

## Already real

- UEFI x86-64 boot and Foundation GPT image;
- PID 1 and isolated Ring-3 ELF applications;
- writable FAT32/VFS persistent root;
- AHCI/GPT storage path;
- E1000/PCnet/VirtIO network qualification paths;
- DHCP, DNS, TCP, HTTP and HTTPS/TLS transport;
- AC'97 output and public bounded userspace playback;
- WindowManager focus, move, resize, minimize, maximize and close;
- UI ABI v2 native widget scenes with stable widget IDs and icons;
- window-local pointer adapter after move/resize/maximize;
- Kurogane icon pack and runtime icon registry;
- distinct UI, Display and Mono bitmap font faces;
- Blade Launcher using the real process-spawn backend;
- Kurosh terminal;
- Vault backed by the public VFS;
- Forge Control backed by real audio/network/system functions;
- Pulse exposing real available state and marking missing services pending;
- Anvil KIDX1/KPKG1 package client, dependency/conflict handling and
  transactional `.new`/`.old` file replacement;
- Anvil repository configuration at `/etc/anvil.cfg`;
- Kurogane Web HTTP/HTTPS navigation, redirects, title/text/link extraction,
  history and link activation;
- QEMU/OVMF and ISO qualification automation.

## Forged Steel visual conversion in progress

The approved target tokens are fixed and must not drift:

```text
Obsidian      #090E0E
Forged Steel  #171C22
Ash           #A8AFB8
Crimson       #E62932
Hot Edge      #FF4A45
```

Current conversion includes:

- rebuilt desktop backdrop and identity rail;
- forged/chamfered window chrome;
- enlarged Kurogane Spine visual treatment;
- Blade-specific asymmetric card renderer with indexed rail markers;
- shared Forged Steel native surfaces;
- Kurogane logo/icon assets in shell chrome;
- crisp single-pass system typography;
- browser shell visible before blocking DNS/TCP/TLS navigation.

Still required before the visual system is considered 5.0:

- spatial Vault layout: Locations + file/project field + Preview;
- Forge Control radial/control-board composition;
- compact Pulse flyout anchored to shell state;
- Anvil graph/list/install-queue composition;
- notification notch/toast anchor;
- Recent Files and System Actions Blade sections backed by real services;
- final wordmark proportions and scalable font assets;
- complete removal of user-visible legacy Flux terminology;
- final window/spine geometry validation at 1280x720, 1280x800, 1600x900 and
  1600x1200.

## Performance blockers

The compositor already keeps a RAM front shadow, so frame comparison does not
scan GOP/VRAM on every present. System typography also no longer performs a
second fake-shadow glyph raster pass.

The following work is still required:

1. Replace WindowManager `DirtyMode::Full` with bounded dirty rectangles.
2. Stop software-cursor background capture from reading GOP for every pointer
   move; cursor background should come from a RAM-presented surface/cache.
3. Add per-window/per-widget damage propagation.
4. Skip scene composition for fully occluded/minimized surfaces.
5. Measure frame time and present bytes instead of using a synthetic FPS claim.
6. Keep live metrics redraw bounded and avoid redraw when values did not change.

Release acceptance: normal desktop interaction must not visibly stutter in the
reference QEMU accelerated path and must remain usable under TCG fallback.

## Web blockers

Kurogane Web must always create and present its chrome before network work. A
network failure must leave the browser interactive rather than looking like a
failed launch.

Before 5.0:

- move DNS/TCP/TLS requests behind an async request/event service;
- add request cancellation/timeouts visible to userspace;
- improve HTML/CSS layout and independent page typography;
- keep unsupported JavaScript/Web APIs explicitly unsupported rather than
  silently faking success.

A Chromium/Blink port is not a 5.0 release prerequisite unless its required
POSIX/thread/socket/sandbox dependencies are actually implemented.

## Anvil blocker

The configured external repository is currently:

```text
https://github.com/KrzysioYT/KuroganeOS-Packages
```

The client already targets:

```text
HOST=raw.githubusercontent.com
BASE=/KrzysioYT/KuroganeOS-Packages/main
```

The external repository must exist and contain a valid signed/release catalog
before Anvil can be considered production-ready. HTTPS transport alone is not
package authenticity; signed metadata or package cryptographic hashes are still
required.

## Version-change gate

Do **not** set `KUROGANE_VERSION_STRING` to `5.0.0` until all items below pass:

- Boot -> Secure Access -> Blade succeeds repeatedly.
- Blade launches Kurosh, Vault, Anvil, Forge Control, Pulse and Web through the
  normal Ring-3 process backend.
- Closing/crashing one application does not terminate the session root.
- Web chrome appears immediately even with network disabled.
- Vault reads and mutates the real VFS where permitted.
- Anvil installs and rolls back a real package from the external repository.
- Missing Wi-Fi/Bluetooth/battery/update/security services remain visibly
  disabled/pending.
- Forged Steel shell matches the approved design board closely enough that no
  generic debug/list UI remains in the primary experience.
- Frame-time/performance acceptance passes at the reference resolutions.
- Full host tests pass.
- QEMU Foundation boot/integration tests pass.
- QEMU ISO/OVMF qualification passes.
- Oracle VirtualBox install -> reboot -> network qualification passes.
- Windows, Linux and macOS release media contain the same system applications
  and configuration contract.

Until that gate is green, the system remains a development build preparing the
5.0 architecture and visual language rather than claiming a finished 5.0
release.

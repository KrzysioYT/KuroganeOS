# KuroganeOS 5.0 — Forged Steel Desktop

This document is the implementation contract for the final KuroganeOS 5.0
visual and interaction system. The source of truth is the Forged Steel design
board built around the **Kurogane Spine**, **Blade Launcher**, **Vault**,
**Anvil**, **Forge Control** and **Pulse**.

The design slogan is:

> BUILT IN STEEL. REFINED IN FIRE.

KuroganeOS must not fake platform capabilities merely to resemble the design.
Every interactive control shown as available in a release build must map to a
real kernel/userspace service. Missing Wi-Fi, Bluetooth, battery, package or
security services remain disabled/pending until implemented.

## Final design tokens

Shared tokens live in `userspace/gui/theme.h`.

- Obsidian: `#090E0E`;
- Forged Steel: `#171C22`;
- Ash: `#A8AFB8`;
- Crimson: `#E62932`;
- Hot Edge: `#FF4A45`;
- primary text: near-white steel;
- success green and warning amber are reserved for state feedback only.

The system should read as engineered metal, carbon weave and glowing heat at
edges. Red is used as an energy/focus signal rather than filling large areas.

## Surface map

### Kurogane Spine

The left-side system spine is the persistent shell anchor. It owns shell state,
workspace identity, Blade expansion, quick system actions and collapsed/expanded
state. The final native compositor will render it as an angular vertical rail.

### Blade Launcher

Blade replaces the generic application list. Its primary entries are:

- Kurosh — terminal and developer shell;
- Vault — file manager;
- Anvil — package manager;
- Forge Control — system settings;
- Recent Files — shell-backed recent documents view;
- System Actions — lock/logout/shutdown/restart once power/session services exist.

The current Ring-3 launcher is already being migrated to Blade naming while the
legacy `RED FLUX HOME` window title remains an internal compatibility role until
title-string coupling is removed from WindowManager.

### Vault

Vault is the spatial file manager. The final layout contains Locations,
project/file capsules, storage state and a preview pane. It must continue to use
the real VFS rather than a static mock directory.

### Anvil

Anvil is the package manager and repository client. It is not a decorative
Store. Its package graph, install queue, dependencies and installed state must
be backed by a real package protocol.

### Forge Control

Forge Control is the settings surface. Display, Performance, Audio, Network,
Power, Users, Security and Updates appear only when their services are real.

### Pulse

Pulse is a compact status/control surface for network, audio, performance,
power and radios. The compatibility implementation reports real available
services and explicitly marks unavailable services as pending.

## Milestone A — Forged Steel application foundation

- [x] final Forged Steel design tokens added;
- [x] Blade naming/identity introduced in the Ring-3 launcher;
- [x] Vault remains backed by real VFS `readdir` navigation;
- [x] Anvil application added to the desktop build;
- [x] Forge Control remains backed by real network/audio/system functions;
- [x] Pulse compatibility surface added for live service status;
- [x] Terminal, Performance, System Monitor and About share the central theme;
- [ ] finish Kurosh, Vault, Forge Control, Login and Web visible labels/chrome;
- [ ] replace remaining Red Flux user-facing terminology;
- [ ] introduce final KuroganeOS wordmark/logo assets as generated resources.

## Milestone B — native surface/compositor ABI

The current public UI ABI still serializes application scenes into a bounded
`ku_ui_frame` text transport. This is intentionally retained for compatibility,
but it cannot reproduce the reference design faithfully.

KuroganeOS 5.0 requires:

- [ ] versioned native application surface ABI alongside UI ABI v1;
- [ ] Ring-3 owned pixel/surface buffers;
- [ ] bounded dirty/damage rectangles;
- [ ] native widget records with geometry, state and stable IDs;
- [ ] pointer hit-testing with window-local coordinates;
- [ ] keyboard focus traversal independent of serialization order;
- [ ] clipping per widget and application surface;
- [ ] alpha-aware composition;
- [ ] rounded/angled panel primitives and steel/hot-edge border treatment;
- [ ] soft shadows and glow implemented with bounded software fallbacks;
- [ ] crash-safe surface ownership cleanup;
- [ ] compositor memory accounting and hard per-process limits.

This milestone is what turns the current functional compatibility UI into the
actual Forged Steel desktop shown in the design board.

## Milestone C — typography, fonts and icons

- [ ] UTF-8 decoding in desktop text paths;
- [ ] scalable system font rasterizer with measured glyph metrics;
- [ ] regular/medium/bold system faces;
- [ ] monospace Kurosh face;
- [ ] browser page fonts selected by page/CSS independently from system UI;
- [ ] text clipping, ellipsis and measured layout;
- [ ] Kurogane icon family for Blade, Kurosh, Vault, Anvil, Forge, Pulse,
      Web, Security, Power and system actions;
- [ ] HiDPI scale factor in the public UI contract;
- [ ] generated fallback font resources included by every platform build.

## Milestone D — Kurogane Spine and shell states

- [ ] replace title-string session identity with explicit shell/application role;
- [ ] collapsed Spine;
- [ ] Blade cards open state;
- [ ] Pulse expanded state;
- [ ] notification notch/toast anchor;
- [ ] workspace selector;
- [ ] pinned/running/focused application state;
- [ ] recent files service;
- [ ] session lock/logout;
- [ ] shutdown/restart after power service lands;
- [ ] context menus, mouse wheel and clipboard;
- [ ] full keyboard usability without pointer input.

## Milestone E — Vault

Already functional:

- [x] real directory enumeration;
- [x] directory enter/parent/home/root navigation;
- [x] regular file preview;
- [x] ELF application launch.

Required for the final design:

- [ ] Locations sidebar backed by VFS/mount state;
- [ ] project capsules/grid and table modes;
- [ ] Name / Type / Size / Modified metadata;
- [ ] file/folder icons;
- [ ] create directory;
- [ ] rename;
- [ ] delete with confirmation;
- [ ] breadcrumbs;
- [ ] multi-selection;
- [ ] storage bars from real volume statistics;
- [ ] preview providers outside the kernel;
- [ ] friendly read-only behavior on Try/live media;
- [ ] synced/remote locations only after an actual remote filesystem service.

## Milestone F — Forge Control and Pulse services

Existing real controls:

- [x] audio master volume/mute;
- [x] kernel network state;
- [x] CPU/graphics/memory runtime snapshot;
- [x] browser launch for connectivity testing.

Still required:

- [ ] display settings service;
- [ ] performance profile service;
- [ ] wired network configuration service;
- [ ] Wi-Fi driver/control service;
- [ ] Bluetooth driver/control service;
- [ ] battery/power service;
- [ ] users/account service;
- [ ] security policy service;
- [ ] update service;
- [ ] notification settings;
- [ ] persisted appearance/accent preferences.

Until those exist, Forge/Pulse must say `PENDING` instead of exposing fake
working toggles.

## Milestone G — Anvil package system

Anvil packages live in a **separate GitHub repository**. The OS build only
contains `/etc/anvil.repo` pointing to the repository host/base path.

Default build target:

```text
HOST=raw.githubusercontent.com
BASE=/KrzysioYT/KuroganeOS-Packages/main
```

Both values are build-overridable with `ANVIL_REPO_HOST` and
`ANVIL_REPO_BASE`.

### Repository index v1

`index.kuro`:

```text
KIDX1
pkg|kuro-shell|5.0.0|Kurosh terminal|/packages/kuro-shell/5.0.0/manifest.kuro
pkg|kuro-protocols|5.0.0|Protocol support|/packages/kuro-protocols/5.0.0/manifest.kuro
```

### Package manifest v1

```text
KPKG1
name=kuro-shell
version=5.0.0
destination=/apps/kuro-shell
payload=/packages/kuro-shell/5.0.0/payload
bytes=123456
depends=kuro-core,kuro-protocols
peer=
conflicts=kuro-shell-legacy
```

Current Anvil implementation provides:

- [x] HTTPS catalog download over the existing TLS stack;
- [x] separate repository configuration staged by Linux/macOS/Windows builds;
- [x] package index parsing;
- [x] manifest parsing;
- [x] normal dependency auto-install;
- [x] peer dependency requirement semantics;
- [x] conflict checks;
- [x] bounded package payload download;
- [x] 16 KiB chunked VFS writes;
- [x] `.new` / `.old` transactional replacement with rollback attempt;
- [x] installed-package database in `/home/anvil.db`;
- [x] package payload size verification;
- [ ] semantic version constraints (`>=`, `^`, `~`, ranges);
- [ ] cycle detection independent of recursion-depth guard;
- [ ] SHA-256/signature verification through a public crypto service;
- [ ] uninstall transaction;
- [ ] package-owned file database;
- [ ] update transaction;
- [ ] repository metadata signing;
- [ ] package capabilities/permissions;
- [ ] command-line `anvil` client using the same backend;
- [ ] dynamic app registry refresh after installation.

Transport authenticity currently comes from HTTPS/TLS and exact byte-count
validation. Release-grade package authenticity still requires signed repository
metadata or per-package cryptographic hashes; the GUI must not claim packages
are cryptographically verified until that lands.

## Milestone H — browser and graphics

- [ ] Kurogane Web chrome adapted to Forged Steel without regressing HTTP/HTTPS;
- [ ] independent browser font stack;
- [ ] HTML/CSS rendering path continues toward Chromium/Blink port;
- [ ] D3D compatibility layers remain separate from the desktop UI ABI;
- [ ] desktop compositor can move to the Kurogane Graphics backend without
      changing application widget semantics;
- [ ] unsupported D3D feature levels are never advertised.

## Milestone I — performance and release quality

- [ ] no full-screen flicker while Pulse/Monitor updates;
- [ ] bounded frame time at 1280x720 and 1600x900 on software rendering;
- [ ] dirty-region compositor validation with overlapping windows;
- [ ] application crash cannot kill the shell;
- [ ] QEMU/OVMF smoke test;
- [ ] VirtualBox reference-profile smoke test;
- [ ] Linux, Windows and macOS build pipelines stage the same desktop apps and
      Anvil repository config;
- [ ] package repository failure leaves the desktop usable;
- [ ] offline boot never blocks waiting for Anvil.

## KuroganeOS 5.0 definition of done

The version string may become `5.0.0` only when all of the following are true:

1. Boot -> Login -> Kurogane Spine/Blade works repeatedly.
2. The native compositor reproduces the Forged Steel shell rather than a
   12-line compatibility approximation.
3. Blade, Kurosh, Vault, Anvil, Forge Control, Pulse and Web share one design
   system and font/icon family.
4. Pointer and keyboard activate the same native widgets.
5. Vault performs real filesystem operations through the public VFS API.
6. Anvil installs real packages from the external repository and handles
   dependencies without corrupting the root filesystem.
7. Forge Control and Pulse expose only real services.
8. Browser keeps functional TLS/web navigation and independent page typography.
9. A crashing Ring-3 app cannot take down the shell/compositor.
10. QEMU/OVMF and VirtualBox runtime qualification pass.
11. Linux, Windows and macOS release builds produce consistent media.
12. No fake package, Wi-Fi, Bluetooth, battery, security or update state is
    presented as functional.

## Compatibility policy

KuroganeOS 5 work lands incrementally. Existing Ring-3 binaries using UI ABI v1
must continue to run while the native surface ABI is introduced. Internal
compatibility titles such as `RED FLUX HOME` can remain until an explicit shell
role replaces them, but user-facing surfaces should use the final KuroganeOS 5
terminology.

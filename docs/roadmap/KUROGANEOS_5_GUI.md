# KuroganeOS 5 GUI — Obsidian Desktop

This document is the implementation contract for the KuroganeOS 5 desktop UI.
The visual reference is the dark KuroganeOS concept built around an Obsidian
surface palette, steel typography and a restrained crimson accent.

The target is not a static mock-up. Every visible control must map to a real
kernel/userspace capability, or be clearly disabled until its service exists.
The project must not fake Wi-Fi, package installation, GPU acceleration or
other capabilities that are not implemented.

## Visual direction

Shared application tokens live in `userspace/gui/theme.h`:

- Obsidian background: `#0B0D10`;
- graphite surfaces: `#15181D` / `#1B1F25`;
- steel borders and secondary text;
- primary text near white;
- crimson accent: `#E32636`;
- green only for success/connected state;
- amber only for warning state.

The desktop should feel precise and technical rather than retro. Red is a focus
and identity signal, not a full-surface fill.

## Milestone A — application layer foundation

- [x] shared KuroganeOS 5 Obsidian application palette;
- [x] Home/launcher content restyled around the new visual language;
- [x] legacy `RED FLUX HOME` surface title retained as an internal session-root
      compatibility contract until WindowManager stops using a title string as
      identity;
- [x] Files upgraded from a fixed quick-access demo to real VFS `readdir`
      navigation;
- [x] Files can enter directories, go to parent/home/root, refresh, inspect
      regular files and launch ELF applications;
- [x] Settings split into Network, Appearance, Audio and System sections;
- [x] Settings uses real AC'97 audio state/control and real app launches;
- [x] Terminal, Performance, System Monitor and About aligned to the shared
      Obsidian language;
- [ ] Browser content chrome aligned to the Obsidian component system without
      regressing HTTP/HTTPS/TLS diagnostics;
- [ ] Login/setup surfaces aligned with the final KuroganeOS 5 identity.

## Milestone B — native UI transport

The current public UI ABI is still `ku_ui_frame`: a compatibility transport of
12 text lines plus a progress value. It cannot represent the reference GUI
faithfully. KuroganeOS 5 therefore requires a native widget/surface path rather
than adding more formatting conventions to text lines.

Required work:

- [ ] introduce a versioned native surface ABI while retaining UI ABI v1 during
      migration;
- [ ] per-window pixel/surface buffers owned by Ring 3 applications;
- [ ] bounded dirty/damage rectangles instead of full content redraws;
- [ ] native widget records with stable IDs and geometry;
- [ ] pointer events routed with local window coordinates and widget hit target;
- [ ] keyboard focus traversal independent of visual serialization order;
- [ ] clipping per widget and per application surface;
- [ ] alpha-aware surface composition;
- [ ] shared component primitives: panel/card, button, input, toggle, checkbox,
      radio, slider, tabs, list/table rows, notification and progress;
- [ ] rounded rectangle and soft shadow primitives that degrade cleanly on the
      software renderer;
- [ ] keep compositor memory bounded and validate all Ring-3 buffers at the
      syscall boundary.

## Milestone C — typography and icons

- [ ] UTF-8 decoding in the desktop text path;
- [ ] scalable system font rasterizer with measured glyph metrics;
- [ ] regular/medium/bold faces and fallback selection;
- [ ] text clipping and ellipsis based on measured width;
- [ ] independent browser font selection so page CSS does not inherit the
      system UI font unconditionally;
- [ ] vector/primitive Kurogane icon set for Home, Terminal, Files, Web,
      Settings, Monitor, Store, Mail, Calendar, Music and Photos;
- [ ] HiDPI scale factor in the public UI contract.

## Milestone D — desktop shell

- [ ] remove title-string coupling from Home/session identity and replace it
      with a stable surface/application role;
- [ ] final centered dock with pinned/running/focused states;
- [ ] desktop launcher/grid with search and app registry rather than a compiled
      fixed array;
- [ ] desktop shortcuts backed by real app manifests;
- [ ] notification area for network/audio/power/clock service state;
- [ ] wallpaper/resource loading with a safe built-in fallback;
- [ ] context menus, mouse wheel and clipboard;
- [ ] keyboard navigation remains fully usable without pointer input;
- [ ] clean session shutdown/logout without orphaned application surfaces.

## Milestone E — Files

The current branch provides real directory enumeration. The final reference
layout additionally requires:

- [ ] sidebar Places and Devices generated from VFS/mount state;
- [ ] table columns: Name, Size, Type and Modified when metadata exists;
- [ ] file/folder icons;
- [ ] create directory, rename and delete actions using the existing public FS
      mutation API with confirmation for destructive operations;
- [ ] breadcrumb navigation;
- [ ] selection model for multiple files;
- [ ] text/image preview providers implemented outside the kernel;
- [ ] friendly read-only behavior in Try/live media.

## Milestone F — Settings

Settings must expose only real services.

- [x] Appearance foundation;
- [x] Audio master volume/mute via current audio API;
- [x] Network status entry point through the real browser/network stack;
- [ ] public network settings/status service for wired configuration;
- [ ] Wi-Fi section only after a Wi-Fi driver and control service exist;
- [ ] notifications service and preferences;
- [ ] power service and power UI;
- [ ] users/account service and credential-backed controls;
- [ ] date/time service;
- [ ] default-app registry;
- [ ] persisted theme/accent preferences rather than session-local state.

## Milestone G — Kurogane Store

Do not ship a decorative Store that pretends to install applications.

Before the Store can be considered functional:

- [ ] package manifest format and version rules;
- [ ] package database;
- [ ] dependency resolver with cycle/conflict handling;
- [ ] repository metadata format;
- [ ] HTTPS repository client using the existing TLS foundation;
- [ ] signature/hash verification before installation;
- [ ] atomic install/update/remove transaction model;
- [ ] permissions/capability declaration in manifests;
- [ ] dynamic app registry refresh after package changes;
- [ ] userspace package service API;
- [ ] Store GUI consumes that service for Browse, Installed and Updates views;
- [ ] command-line package client uses the same service, not a second backend.

## Milestone H — performance and compositor quality

- [ ] dirty-region compositor validated under moving/resizing multiple windows;
- [ ] no full-screen flicker during live Performance/System Monitor updates;
- [ ] bounded frame time on the software backend at 1280x720 and 1600x900;
- [ ] memory accounting for every application surface;
- [ ] recovery when a client dies while owning a surface;
- [ ] GPU/native Kurogane Graphics backend can replace scanout acceleration
      without changing application widget semantics;
- [ ] Direct3D compatibility layers remain separate from the desktop UI ABI and
      must never advertise unsupported feature levels.

## KuroganeOS 5 GUI definition of done

The GUI milestone can be called complete only when all of the following are
true on a release candidate:

1. Boot -> Login -> Home works repeatedly without developer-console input.
2. Home, Dock, Files, Terminal, Web, Settings and Monitor share one component
   and typography system.
3. Normal application content no longer depends on serializing views into 12
   text rows.
4. Pointer and keyboard can activate the same native widgets.
5. Files performs real directory operations through the public VFS API.
6. Settings never presents a working toggle for a service that does not exist.
7. Store is either fully connected to the package service or absent from the
   release UI; a fake installer is not acceptable.
8. Browser preserves functional HTTP/HTTPS/TLS navigation while adopting the
   desktop chrome and independent page typography.
9. A crashing application cannot take down the desktop session or compositor.
10. QEMU/OVMF and the reference VirtualBox profile pass runtime GUI smoke tests.
11. No release string is changed to `5.0.0` until these acceptance conditions
    and the release checklist pass.

## Compatibility policy during development

KuroganeOS 5 work should land incrementally behind compatible APIs. Existing
Ring-3 binaries using UI ABI v1 must continue to work while native surface ABI
is introduced. Internal compatibility names such as `RED FLUX HOME` may remain
until the kernel uses explicit role IDs; visual user-facing labels can move to
Kurogane Home immediately.

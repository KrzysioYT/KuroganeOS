# Changelog

## Unreleased

### Road to 15 versioning

- Normalized the formal Road-to-15 sequence: `3.3.3-dev` Red Flux is followed by `3.4.0-dev` System Services; patch-like `3.3.5`-`3.3.9` and `3.4.1` names are internal development workstreams rather than separate formal product releases.
- Recorded `3.3.3-dev — Red Flux` as a QUALIFIED scoped DEV milestone based on completed host/kernel/ABI/SDK/media/OVMF/QEMU/network/TLS regressions, without claiming later Steel/Forge/security/update capabilities.
- Reclassified Oracle VirtualBox host acceptance as OPTIONAL / EXTERNAL VALIDATION, excluded from Definition of Done and progress percentages when the environment is unavailable.

### System Services

- Added a public Service SDK over the real named IPC backend with PID-owned, generation-safe connections and process-exit cleanup.
- Began the real Ring-3 Event Broker (`events.v1`) with bounded subscriptions, unsubscribe, publish dispatch through real waitable kernel events, PID ownership and a public Event Broker protocol.
- Added a QEMU runtime qualification probe for subscribe → publish → event wait → unsubscribe; the current workstream keeps runtime failures explicit and unqualified until the roundtrip passes.

### Licensing

- Adopted KuroganeOS Source-Available License 2.0 (KSAL-2.0) for current revisions.
- Removed the percentage-based copying threshold from the license and clarified independent implementations, interoperability, and protected source expression.
- Added an explicit copyright-term license duration, a 30-day cure path for a first unintentional curable breach, and a limited patent clause.
- Added an express permission for reviews, tutorials, screenshots, videos, livestreams, benchmarks, news reporting, and similar media coverage, including monetized coverage within the license limits.
- Made CLA acceptance mandatory before a contribution can be incorporated into the official repository.
- Synchronized `LICENSE`, `CLA.md`, `NOTICE`, `TRADEMARKS.md`, `CONTRIBUTING.md`, `README.md`, and `docs/LICENSING.md` with the current licensing model.
- Preserved the historical MIT boundary without applying MIT to later revisions.

## 2.2.5 - 2026-08-16

### macOS ISO build

- Added `scripts/build-installer-macos.sh` for native installable UEFI ISO builds on macOS.
- Added `--iso` to `scripts/build-macos.sh` so one command can build the development IMG and versioned installer ISO.
- Made `scripts/build-iso.sh release` route to the native macOS installer builder on Darwin instead of requiring PowerShell.
- The macOS installer builder creates `install.pkg`, a dedicated 64 MiB FAT32 EFI System Partition and an El Torito/UEFI ISO through `xorriso`; it does not convert the development IMG into an ISO.
- macOS release output is `dist/KuroganeOS-2.2.5-x86_64.iso` with `dist/SHA256SUMS.txt` and compatibility copy `kurogane.iso`.
- `setup-macos.sh` now validates `xorriso` as a required tool and prints current IMG/ISO build commands.

### Build regression repair

- Fixed the 2.2.0 Flux Terminal build regression that produced `fatal error: version.h: No such file or directory`.
- Made the GUI Terminal version include independent of the compiler working directory.
- Added `common/` to the Unix/macOS SDK include search path as an additional guard against version-header regressions.

### Version

- Bumped KuroganeOS to 2.2.5.

## 2.2.0 - 2026-08-16

### Desktop Developer Preview

- Promoted the experimental desktop work into the KuroganeOS 2.2 Desktop Developer Preview.
- Introduced the **Kurogane Flux** framebuffer visual language with a signal spine, asymmetric surfaces, status nodes, segmented progress indicators and a floating pulse ribbon instead of a conventional desktop taskbar/dock presentation.
- Preserved WindowManager focus, z-order, dragging, minimize, maximize/restore, close, Alt+Tab and Alt+F4 behavior while changing the presentation layer.
- Removed the stale visual identity that still presented the GUI terminal as "KuroganeOS 2.0 Desktop Alpha".

### Flux Console

- Replaced the tiny Ring-3 ABI probe shell with a usable Flux Console.
- Added `run <name|path>`, `open`, `gui`, `apps`, `which`, `jobs` and `wait` workflows.
- Added PID/TID display, command status, volatile history, logical CWD, `cat/read`, `echo`, overflow-checked `calc`, `sleep`, `yield`, `true` and `false`.
- Added tracked background child processes with periodic reap behavior instead of intentionally leaking finished children as shell zombies.
- Added developer diagnostic shortcuts for memory/process/device/storage views through the Ring-3 System Monitor.
- Legacy privileged command names now report the missing Ring-3 capability instead of being indistinguishable from unknown commands.
- Kept the emergency kernel developer console separate rather than introducing an unsafe generic "execute kernel shell command" syscall.

### GUI Terminal

- Reworked the Ring-3 terminal surface around the Flux identity.
- Added `version`, `pid`, `apps`, `run`, `gui`, `jobs`, `echo`, `about` and `clear` commands.
- Added background child tracking/reaping for applications launched from the GUI terminal.

### Documentation repair

- Replaced stale documentation that still described KuroganeOS 1.0 as the current release and claimed that Ring 3, processes, syscalls, persistent storage, mouse input and the WindowManager did not exist.
- Updated userspace, desktop roadmap, desktop release status, current limitations, build status, macOS workflow, installation and VirtualBox documentation for the 2.2 source tree.

### Compatibility

- Storage, installer, UEFI boot and the macOS development backend from 2.1/2.1.1 remain the foundation of 2.2.
- No generic privileged command bridge was added to the userspace ABI.

## 2.1.1 - 2026-08-16

### macOS development support

- Added a native macOS build path that does not require WSL or Windows PowerShell.
- Added Homebrew environment setup/checking for the x86_64-elf cross-toolchain, QEMU, FAT/GPT image tools and Python.
- Made the kernel Makefile host-aware: macOS uses `x86_64-elf-*` from `PATH`, while Windows keeps the repository-local `.exe` toolchain and PowerShell frontend.
- Added native macOS builds for the x86-64 kernel, basic Ring-3 userspace, the SDK libraries, the external SDK example and the desktop userspace applications.
- Added a portable Python PE32+ converter so `BOOTX64.EFI` can be rebuilt on macOS from the same standalone loader source.
- Added a macOS GPT/FAT32 Foundation-image builder containing the EFI loader, kernel, persistent root filesystem and generated userspace overlay.

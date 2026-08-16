# Changelog

## Unreleased

- No unreleased changes yet.

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
- Added a QEMU macOS runner with Homebrew EDK2 discovery, E1000 user networking, serial logging, required-test failure detection and PID 1/global-success smoke-test gating.
- Added `build-app-macos.sh` for compiling C/C++ applications against the KuroganeOS SDK and optionally installing them into the development root filesystem.
- Added `docs/MACOS_DEVELOPMENT.md` with setup, build, application development and QEMU testing instructions.
- Bumped the product version to KuroganeOS 2.1.1.

### Compatibility

- The guest architecture and application ABI remain x86-64; macOS is a development host, not a separate KuroganeOS target.
- Apple Silicon hosts use QEMU TCG to emulate the x86-64 guest.
- Existing Windows/WSL build and test workflows remain available and are not replaced by the macOS backend.

## 2.1.0 - 2026-08-16

### Installable system

- Added UEFI installation media carrying `BOOTX64.EFI`, the kernel and the installer payload.
- Added explicit SATA/AHCI installation-target discovery.
- Added protective MBR plus primary and backup GPT creation.
- Added FAT32 formatting for the EFI System Partition and the KuroganeOS persistent root.
- Added installation of `EFI/BOOT/BOOTX64.EFI`, the kernel and the userspace filesystem.
- Added installer verification and flush before reporting a successful installation.
- Added installed-system UEFI boot from the virtual HDD after the installation medium is removed.
- Added writable persistent-root mounting and reboot persistence checks.

### Userspace and processes

- Added the x86-64 Ring-3 runtime and the `int 0x80` syscall gate.
- Added private user address spaces, ELF64 process loading and process spawn/wait/exit lifecycle support.
- Added timer-preempted kernel threads and userspace processes.
- Added `/system/init` as the userspace PID 1 entry point and userspace console bootstrap.
- Added userspace shell/app images and experimental Ring-3 desktop applications.

### Storage and platform

- Added/expanded PCI, ACPI MADT and APIC discovery.
- Added SATA/AHCI read, write and flush support.
- Added GPT parsing and writable FAT32/VFS support.
- Added PS/2 keyboard, PS/2 mouse and the common input queue.
- Preserved loopback networking fallback while the physical network stack continues to evolve.

### Release and testing

- Bumped the product version to KuroganeOS 2.1.
- Added canonical versioned installer output under `dist/`.
- Added `dist/SHA256SUMS.txt` generation for the release ISO.
- Kept a generated root `kurogane.iso` only as a compatibility artifact for existing emulator helpers.
- Added generated-artifact ignores so build images and emulator state are not treated as source files.
- Corrected required-test ordering so global success is emitted only after the required PID 1 userspace start succeeds.
- The committed QEMU installer logs cover installer-medium boot, deployment to a blank AHCI disk and the subsequent installed-system boot.
- VirtualBox has an EFI/AHCI smoke-test helper and documented manual installation acceptance flow.

### Licensing

- Current revisions remain licensed under KuroganeOS Source-Available License 1.0 (KSAL-1.0).
- Historical revisions previously distributed under MIT remain documented by `LICENSE-MIT-LEGACY`.

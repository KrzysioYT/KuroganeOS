# Changelog

## Unreleased

- No unreleased changes yet.

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
- Added canonical versioned installer output at `dist/KuroganeOS-2.1-x86_64.iso`.
- Added `dist/SHA256SUMS.txt` generation for the release ISO.
- Kept a generated root `kurogane.iso` only as a compatibility artifact for existing emulator helpers.
- Added generated-artifact ignores so build images and emulator state are not treated as source files.
- Corrected required-test ordering so global success is emitted only after the required PID 1 userspace start succeeds.
- The committed QEMU installer logs cover installer-medium boot, deployment to a blank AHCI disk and the subsequent installed-system boot.
- VirtualBox has an EFI/AHCI smoke-test helper and documented manual installation acceptance flow; an end-to-end VirtualBox installation must not be claimed as automatically verified unless it is actually run in an environment with `VBoxManage`.

### Licensing

- Current revisions remain licensed under KuroganeOS Source-Available License 1.0 (KSAL-1.0).
- Historical revisions previously distributed under MIT remain documented by `LICENSE-MIT-LEGACY`.

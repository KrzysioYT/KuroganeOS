# Repository baseline

Audit date: 2026-07-27. The repository has no Git metadata.

The baseline is a freestanding x86-64 C/C++ kernel loaded by a repository-owned
UEFI application. A clean `scripts/build.ps1 -Rebuild` completed in 2:45 and
produced a 113,464-byte PIE ELF kernel, a 7,680-byte `BOOTX64.EFI`, and a
deterministic 64 MiB FAT32 image. No host runtime is linked into the target.

Both the staged-files boot and FAT32-image boot reached `kurogane:/ $` under
QEMU 11/EDK2. The keyboard-driven system smoke test exercised version, heap and
frame statistics, RAMFS reads, loopback networking, application enumeration,
and GUI launch/exit. Logs are under `build/logs`.

This is a kernel demonstration, not an installable desktop release. There is no
user mode, persistent filesystem, storage driver, installer, first-boot flow,
package manager, recovery environment, or installed-system boot test.

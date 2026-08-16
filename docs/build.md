# Build and packaging

## Supported build host

The maintained workflow is Windows PowerShell with WSL2. The repository owns
the cross compiler and QEMU under `tools/`; WSL2 supplies filesystem/image and
host-test utilities.

Required commands:

- Windows: `powershell.exe`, `wsl.exe`;
- repository: `tools/compiler/x86_64-elf/bin/*`, `tools/qemu/*`;
- WSL2: Bash, `g++`, `make`, Python 3, `fsck.fat`/dosfstools, mtools,
  `xorriso`, `base64`, `wslpath`.

The build does not download tools and does not need host administrator access.

## Kernel and complete media

```powershell
.\scripts\build.ps1 -Configuration debug
.\scripts\build.ps1 -Configuration test -Rebuild
.\scripts\build.ps1 -Configuration release -Clean
```

Profiles change optimization/debug/test flags. `-Rebuild` performs clean plus
build. `-NoStage` stops after the kernel; `-StageOnly` reuses a valid kernel and
rebuilds staged media. A build lock prevents concurrent writers.

The normal pipeline compiles the PIE kernel, legacy and native userspace,
SDK/CRT/libraries/Desktop apps, UEFI loader, deterministic FAT image, 512 MiB
GPT base image and installer package/ISO. It verifies ELF machine/type,
supported kernel relocations, undefined symbols, executable stack/W+X, EFI PE
headers and image geometry.

Important outputs:

```text
build/kernel.elf
build/kernel.map
build/BOOTX64.EFI
build/build-info.txt
build/sdk/sysroot/
build/userspace/rootfs/
kurogane.img
build/images/KuroganeOS-base.img
build/install.pkg
build/images/KuroganeOS-installer.iso
```

`build-info.txt` records version, profile, compiler/linker, flags, enabled
drivers and compatibility layers. `CONFIG_POSIX=n` is deliberate.

## SDK and installer-only commands

```powershell
.\scripts\build-sdk.ps1
.\scripts\build-installer.ps1
```

`build-installer.ps1` requires already built kernel/EFI/userspace artifacts and
regenerates the bounded package and bootable installer ISO. Package paths are
checked against its FAT 8.3 contract.

## Images and safety

`KuroganeOS-base.img` is generated deterministically with GPT, ESP and root.
The mutable developer image under `state/` preserves its root and receives only
explicit ESP updates. QEMU defaults to snapshot mode. Writable tests accept
only explicit `.img`/`.raw` paths and the installer automation confines its
target to `build/test-disks`; never point it at a physical disk.

## Tests and one-command verification

```powershell
wsl.exe bash -lc "cd /mnt/e/KuroganeOS && bash scripts/test.sh"
.\scripts\validate-2.0.ps1 -TimeoutSeconds 180 -StabilitySeconds 120
```

The 2.0 frontend performs a clean build/package, hosted and image tests, named
QEMU boot/userspace/multitasking/filesystem/network/desktop/full-system runs,
two-boot persistence, a long desktop stability run and guarded installation.
`verify.ps1` remains the multi-profile debug/test/release and optional
VirtualBox frontend. See [testing.md](testing.md) for scenario/log details.

## Failure diagnosis

Build commands stop on the first nonzero native exit. Inspect
`build/build-info.txt`, `build/kernel.map` and the relevant log under
`build/logs`. If an incremental result is suspect, use `-Rebuild`; final release
qualification always begins from clean artifacts.

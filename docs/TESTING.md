# Testing and qualification

Hosted tests validate deterministic subsystems. QEMU validates real privilege
transitions, timer preemption, storage, networking, input, graphical session
startup and installer paths. A marker is evidence only when the runner exits
successfully and the serial log contains no `[TEST] ...: FAIL`.

## 1. Host tests

Windows + WSL2:

```powershell
wsl.exe --exec bash -lc "cd /mnt/e/KuroganeOS && bash ./scripts/test.sh"
```

Adjust `/mnt/e/KuroganeOS` to the actual repository path.

The suite covers allocator/paging, Process/Thread, scheduler, ELF/ABI,
RAMFS/VFS/FAT32/GPT/AHCI, input, WindowManager, USB HID, network protocols,
installer package/layout, SDK ABI and GUI contracts.

## 2. Focused Foundation test

Before running the full verifier, use the exact image that contains
`Kurogane Root`:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\run-qemu.ps1 `
  -UseDiskImage `
  -DiskImagePath .\build\images\KuroganeOS-base.img `
  -ShellTest `
  -TimeoutSeconds 90 `
  -MemoryMiB 1024 `
  -LogName focused-foundation
```

The current graphical Foundation path is:

```text
PID1 -> /gui/login -> secure access -> Blade Launcher session root
```

The compatibility switch is still named `-ShellTest`, but the runner now knows
how to qualify both the graphical session and Safe Mode console.

## 3. Full verifier

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\verify.ps1 `
  -TimeoutSeconds 90 `
  -KeepLogs
```

`verify.ps1` stops at the first failure and writes a status log plus per-stage
logs under `build/logs`.

Main stages:

1. WSL2/toolchain preflight;
2. clean debug build;
3. host tests;
4. legacy FAT read-only validation;
5. Foundation GPT/FAT validation;
6. Foundation QEMU integration;
7. Safe Mode integration;
8. clean `test` profile build;
9. AHCI/GPT writable scratch test;
10. release build;
11. release ISO construction;
12. QEMU ISO qualification;
13. optional VirtualBox qualification.

VirtualBox is opt-in in `verify.ps1` unless a release/media pipeline explicitly
requires it.

## 4. Why legacy `kurogane.img` is not the userspace test disk

`kurogane.img` is a 64 MiB FAT/EFI artifact. It does not provide the persistent
GPT `Kurogane Root` partition required by `/system/init` and the current Ring-3
desktop. The verifier still checks its filesystem integrity, but normal PID1
qualification uses:

```text
build/images/KuroganeOS-base.img
```

## 5. GUI performance tests

Do not measure GUI FPS with the deterministic TCG test runner.

Windows interactive performance path:

```powershell
.\scripts\run-qemu-fast.ps1 `
  -Accelerator auto `
  -MemoryMiB 1024 `
  -LogName perf-check
```

Record whether the runner prints:

```text
[active] accelerator=whpx
```

TCG is intentionally supported as fallback but is much slower for the current
software compositor.

When testing responsiveness, exercise at least:

```text
login Enter/click
mouse movement
Blade open/focus
Kurosh launch
Vault launch
Forge Control launch
window focus/move/resize/close
dock activation
```

## 6. Logs

Every QEMU run should use a unique `-LogName`:

```text
build/logs/<LogName>-serial.log
build/logs/<LogName>-stdout.log
build/logs/<LogName>-stderr.log
```

For verifier failures also inspect:

```text
build/logs/verify-*-status.log
build/logs/verify-*-<stage>.log
```

## 7. Installer safety

Installer tests use disposable files under `build/test-disks`. Do not point
installer automation at a physical disk. Writable QEMU attachments always
require an explicit file path.

## 8. What counts as failure

Treat these as immediate failures:

```text
[TEST] ...: FAIL
KERNEL PANIC
KERNEL EXCEPTION
fatal:
QEMU exits before required markers
timeout with required markers missing
```

External DNS/HTTPS can depend on host connectivity, but E1000 link, DHCP, UDP
and gateway transport are part of the Foundation qualification when the
network test is enabled.

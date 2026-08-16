# Testing and QEMU validation

Hosted tests cover deterministic logic; QEMU proves CPU privilege, timer
preemption, hardware/DMA paths, persistence, networking, GUI input and install.
A marker counts only with a successful runner exit and no `[TEST] ...: FAIL`.

## Hosted tests

```powershell
wsl.exe bash -lc "cd /mnt/e/KuroganeOS && bash scripts/test.sh"
```

`build/logs/host-tests.log` covers memory/page permissions, Process/Thread and
context switching, scheduler, ELF/ABI, RAMFS/VFS/FAT32, GPT/Partition/AHCI,
input/WindowManager/USB HID, network protocols, installer layout/package and
runnable SDK project generation.

## QEMU matrix and logs

Use unique `-LogName` values. Each run writes separate `-serial`, `-stdout` and
`-stderr` logs under `build/logs`.

| Scenario | Runner | Required evidence |
|---|---|---|
| boot | `run-qemu.ps1 -UseDiskImage -Headless` | prompt, no panic |
| userspace | add `-ShellTest` | PID1/shell/apps/external ELF |
| multitasking | `-ShellTest` | kernel + Ring3 preemption |
| filesystem/network | `-ShellTest` on base image | FAT mount + E1000/DHCP/ICMP |
| persistence | `test-persistence.ps1` | prepare and verify boots |
| desktop | `-DesktopMode -ShellTest` | five apps + PS/2 drag/close |
| safe mode | `-SafeMode` | emergency Ring 0 prompt, minimal drivers |
| USB | `-UsbTest` | xHCI enumeration + injected HID key |
| installer | `test-installer.ps1` | deploy + two HDD-only boots |
| full system | `validate-2.0.ps1` | clean build/tests/images/QEMU/installer |

```powershell
.\scripts\run-qemu.ps1 -UseDiskImage `
  -DiskImagePath .\build\images\KuroganeOS-base.img `
  -Headless -ShellTest -TimeoutSeconds 180 -LogName qemu-userspace

.\scripts\run-qemu.ps1 -UseDiskImage `
  -DiskImagePath .\build\images\KuroganeOS-base.img `
  -Headless -DesktopMode -ShellTest -TimeoutSeconds 180 `
  -LogName qemu-desktop
```

The QEMU monitor injects real emulated keyboard/mouse packets; the runner does
not write expected text into serial logs. Key markers include
`ALL_REQUIRED_TESTS_PASSED`, `ring3_fault_isolation`, both preemption proofs,
FAT persistence, `network_gateway_icmp`, `external_sdk_application`, all
desktop apps, `window_drag_input` and `installer_complete`.
The desktop scenario also requires `window_close_input`; the full validator
includes separate safe-mode and USB runs plus a 120-second stability run by
default.

Persistence and installer targets are purpose-built files in
`build/test-disks`. Installer automation requires a blank 512 MiB disk, exact
`INSTALL`, detaches the ISO, then boots the same disk twice. Never attach a
physical disk.

For stability observation use a long unique-log QEMU run, exercise processes,
filesystem, network and several GUI apps, and inspect for faults, FAIL markers,
deadlock/starvation and cleanup failures. External DNS/HTTP/ICMP probes can
depend on host connectivity; E1000/DHCP/UDP/gateway are required.

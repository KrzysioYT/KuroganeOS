# Testing and QEMU validation

Hosted tests cover deterministic logic; QEMU proves CPU privilege, timer
preemption, hardware/DMA paths, persistence, networking, GUI input and install.
A marker counts only with a successful runner exit and no `[TEST] ...: FAIL`.

## Current Flux 8 acceptance

Use `.github/workflows/qualify-8.0-flux-desktop-foundation.yml` for the scoped
host/UBSan and USB-only QEMU gate. It verifies authenticated Home and real
Files/Terminal child processes plus presented surfaces. Windows/Linux userspace
inventory parity and PowerShell parsing are separate build checks.

Run `bash scripts/run-host-tests.sh` for host regression. A skipped test must be
reported, never silently counted as PASS. VirtualBox interactive acceptance
is required separately; see [VIRTUALBOX.md](VIRTUALBOX.md). Record source SHA,
media SHA256, VM/device profile, log and runner result. A documentation-only
change does not requalify changed runtime code.

The older scenario runners below remain specialized tools; `validate-2.0.ps1`
is not the complete release gate for 8.0 or 15.0.

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

## Flux window lifecycle through QMP

The Flux foundation workflow also boots a fresh guest for a stateful lifecycle
scenario. It sends real USB keyboard/tablet events; `[WM]` serial observations
come from completed compositor operations, with generation-safe window IDs,
owner PIDs, bounds, focus and retained-surface counters. Titles and application
contents are not logged. Metadata never grants session/window permissions.

```bash
bash scripts/smoke-uefi-iso-qemu.sh \
  build/images/KuroganeOS-linux.img --disk --smp 4 \
  --nic none --no-ps2 --usb-keyboard --usb-tablet --flux-lifecycle \
  --log-dir build/tests/flux-8-lifecycle --timeout 240 \
  --send-key-after-marker '[TEST] live_login_profile: PASS' ret
```

For a setup ISO, omit `--disk`, use its path, send `ret` after
`installer: select setup mode:`, and use `--send-key-after-marker-count
'[TEST] live_login_profile: PASS' 1 ret` for Login. Use a fresh log directory.

The scenario verifies normal/maximized minimize-and-restore, drag, resize,
Files/Terminal focus switching, close and generation/PID-changing relaunch.
Three additional close/relaunch cycles must return retained window, surface
and payload-byte counts to the same baseline. It records `lifecycle.json` and
PPM screenshots; any missing fresh transition, guest FAIL or assertion failure
returns nonzero. It does not inject expected markers into guest logs or count
old transitions as evidence for a new input. Parser/evidence regressions run
with `python3 tests/test_flux_lifecycle_qmp.py`.

This covers Terminal and Files in a fresh live session, not the complete app,
crash/recovery, installed persistence, Classic, or VirtualBox acceptance matrix.

## Portable installer / two disk-only boots

```bash
python3 scripts/qualification/installer-qemu.py \
  --iso dist/KuroganeOS-7.0.0-dev-x86_64.iso \
  --output build/tests/flux-8-installed --timeout 240
```

The output directory must not exist. The runner creates its own 512 MiB raw
file with exclusive creation; it never accepts an existing target disk. Only
that file and the read-only ISO are attached during installation. Real USB
input chooses Install, the default English/user/no-password profile, and the
sole test disk. It first cancels at the erase prompt and verifies the entire
blank-disk hash is unchanged, then confirms INSTALL and requires the guest's
payload verification and completion marker.

The installer VM stops before two separate disk-only boots. Both must reach
an authenticated Home; the first must finish the first-boot transaction and
the second must report persisted installation state. The runner rejects
fallback to live media, guest FAIL markers and failed QEMU exits. It retains
serial logs, screenshots, the disk and a manifest with ISO/disk/harness hashes.
CI uploads evidence without the large writable disk image. Existing output
and symlink refusal regressions run via `tests/test_installer_qemu_safety.py`.
This covers the default no-password account on emulated AHCI; it is not a
password, upgrade, settings-persistence, power-loss or physical-install gate.

## Settings keyboard accessibility

Settings now supports Tab/Shift+Tab focus, Enter/Space activation, and automatic
scrolling to the focused control. Mouse selection shares that focus and a scene
rebuild after an action preserves it. Control/Alt chords remain reserved.
`test_libui_pointer.c` exercises the production keyboard policy with disabled
and hidden controls, forward/reverse wrap, scrolling and activation.

`--flux-lifecycle` additionally launches Settings with Ctrl+Alt+F7 and captures
keyboard focus at both ends of the form. Add `--flux-settings-write` only with
`--disk` and a writable installed root to verify Enter and Space through actual
theme SET/GET and desktop application. An ISO Try package is read-only; its
settings write failure must not be mistaken for a keyboard transport failure.
`installer-qemu.py --desktop-lifecycle` enables both checks on the first installed
boot, then verifies the ordinary second disk-only boot. The installed runner now compares the exact nondefault theme value saved
by Settings with the value restored and applied by Home after reboot.

## Extended core audit (macOS and Linux)

Run both host entry points: `scripts/run-host-tests.sh` covers the current
driver/desktop suite; `scripts/test.sh` additionally executes cooperative
thread/context-switch and process tests. On Apple Silicon invoke them through
`arch -x86_64 /bin/bash` with an x86-64 host compiler. The assembly host path
uses Mach-O directives and symbol decoration while retaining the actual switch
instructions; production ELF remains unchanged.

`bash scripts/test-core-ubsan.sh` checks allocator boundaries, page mappings,
ELF validation, Scheduler 2.0, ticket-lock contention, IPC, shared memory, stale
file handles, session ownership, hostile package paths and ABI. These are
bounded regression tests, not an exhaustive syscall fuzzer or a proof of
freedom from data races. Real guest SMP and resource churn remain separate.

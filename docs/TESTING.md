# Testing and QEMU validation

Use the [Windows 11 VirtualBox matrix](FLUX8_VIRTUALBOX_MATRIX.md) for
visible Flux acceptance and record the source SHA and ISO SHA-256 per run.
The host framebuffer regression checks that changing the desktop session
indicator leaves the adjacent label intact at all four text scales.

The Flux foundation gate boots the installer ISO in Try mode and clicks
**Next color**. It checks that Settings applies Violet through the settings
service and that the framebuffer uses the new palette in the Dock, Home,
Settings, Files, and Terminal. The same run exercises text scaling from the
keyboard. On a read-only root, Settings labels changes **Session only** and
settingsd keeps them in memory. The installed-disk test separately checks a
durable save and verifies the same values after a disk-only reboot.

The installed lifecycle also scrolls Files at 200% from the left margin to
reach **New folder** and **Rename selected**. It creates `flx8qa`, renames it to
`flx8ok`, and checks the disposable FAT32 root after the second boot. Wheel
input over directory tiles still pages the directory. On the Live ISO, Files
labels and disables mutations on the read-only mount.

The installed test then focuses Terminal at 200%, verifies the prompt in the
framebuffer, types `ls`, and checks that empty `echo`, `echo j`, and `echo k`
produce distinct rendered output. History uses the physical arrow keys.
The Live test also searches Home for Files, opens and closes the result, then
scrolls to Log out and verifies a fresh login and desktop at 200%.

## Settings IPC backpressure regression

`tests/test_flux_theme_failure.c` runs the production Settings appearance
handler against injected service and compositor failures. It verifies that a
rejected theme restores the previous saved value, including an absent key, and
reports an unverified rollback. A timeout after a committed write invalidates
the connection before readback and rollback, so a late reply cannot become the
next request's result. The same handler serves style, wallpaper and scale.

`tests/test_settings_service.c` exercises the production settings daemon with
bounded fake IPC queues. A blocked reply retains its original value, stops
consuming requests on that connection, and does not stall other clients.
Authorization failures are also retried; terminal send failures close and
clear the client. The test reproduced a dropped reply before the fix and is
part of `scripts/run-host-tests.sh`. This host test does not qualify settings
disk-write failure recovery or per-user storage isolation.

`tests/test_settings_persistence.c` injects a failed replacement rename with
the FAT32 no-overwrite contract. It checks rollback in memory, restart recovery
from the previous database, recovery when the rollback rename also fails, and
a subsequent successful save/delete/reload. `settingsd` retains `settings.bak`
and reads it only when `settings.db` is absent; a corrupt current database is
not silently replaced with stale preferences. These bounded host checks do
not establish power-loss atomicity of FAT32 or physical-media durability.

Hosted tests cover deterministic logic; QEMU proves CPU privilege, timer
preemption, hardware/DMA paths, persistence, networking, GUI input and install.
A marker counts only with a successful runner exit and no `[TEST] ...: FAIL`.

## Current Flux 8 acceptance

Use `.github/workflows/qualify-8.0-flux-desktop-foundation.yml` for the scoped
host/UBSan and USB-only QEMU gate. It verifies authenticated Home and real
Files/Terminal child processes plus presented surfaces. Windows/Linux userspace
inventory parity and PowerShell parsing are separate build checks.

Run `bash scripts/run-host-tests.sh` for optimized host regression. A skipped
test must be reported, never silently counted as PASS. VirtualBox qualification
is required separately; see [VIRTUALBOX.md](VIRTUALBOX.md). Scripted
VirtualBox boot/install evidence does not replace the required visible
mouse/keyboard acceptance. Record source SHA, media SHA256, VM/device profile,
log and runner result. A documentation-only change does not requalify changed
runtime code.

The active integration workflow uploads the ISO used by its QEMU job with
`source-sha.txt` and `iso-sha256.txt` for manual VirtualBox acceptance. The
latest scoped results and outstanding gates are in
[DEVELOPMENT_STATE.md](../DEVELOPMENT_STATE.md).

The older scenario runners below remain specialized tools; `validate-2.0.ps1`
is not the complete release gate for 8.0 or 15.0.

## Historical exact-runtime evidence — `64577459`

Runtime source: `6457745962fde73c81a7004bce13cea6992a85c1`; system identity
remains `7.0.0-dev`. At 100/125/150/200%, native Ring-3 command geometry follows
the persisted scale and pointer/scroll input is mapped back to app logical
coordinates. The 100% path is unchanged. This does not yet scale app-icon
internals, desktop/window chrome, display resolution or per-user preferences.
All builds and VM disks below were isolated from existing repository media.

| Check | Result |
| --- | --- |
| `HOST_TEST_DIR=/tmp/kuro-ui-scale-host-final bash scripts/run-host-tests.sh` | PASS — full optimized host regression |
| `HOST_TEST_DIR=/tmp/kuro-ui-scale-ubsan bash scripts/test-core-ubsan.sh` | PASS — 11 core UBSan groups |
| `bash scripts/build-linux.sh --configuration debug --rebuild --no-image` | PASS — exact-SHA Debug kernel/userspace and SDK |
| `bash scripts/build-installer-linux.sh --configuration release --rebuild` | PASS — exact-SHA Release installer/ISO and 20 structure checks |
| `python3 scripts/qualification/installer-qemu.py --iso dist/KuroganeOS-7.0.0-dev-x86_64.iso --output /tmp/kuro-ui-scale-exact-qemu-64577459 --timeout 240 --desktop-lifecycle` | PASS — read-only recovery, safe cancellation, install/two disk boots and persisted settings |
| QEMU Settings scale persistence | PASS — `125%` restored after reboot |
| QEMU USB-only Try (no disk/PS/2; USB HID keyboard/tablet) | PASS — zero block devices, report-pointer click, Login and Home |
| VirtualBox 7.2.20 Try/Login/Desktop, exact ISO | PASS — automated/headless |
| VirtualBox 7.2.20 EFI/AHCI install, disk-only boot, FAT32/PID 1 and PCnet NAT | PASS — fresh temporary VDI |
| GitHub Actions `reliable-file` [41352989065](https://github.com/KrzysioYT/KuroganeOS/actions/runs/41352989065) | PASS |
| GitHub Actions CLA [41352989090](https://github.com/KrzysioYT/KuroganeOS/actions/runs/41352989090) | PASS |
| GitHub Actions `iso-qualification` [41352989006](https://github.com/KrzysioYT/KuroganeOS/actions/runs/41352989006) | PASS — ABI/SDK, regression, IMG/ISO, FAT32/VFS, ISO x20, OVMF and three NAT profiles |
| Visible/manual VirtualBox mouse/keyboard and scaled-control hit-testing | UNVERIFIED |

Exact ISO SHA-256:
`0b8b0b197bf0455a7e06939597cb94c769bed331174cb675af3320deced63531`.
QEMU manifest `/tmp/kuro-ui-scale-exact-qemu-64577459/result.json` binds that
ISO to runtime SHA `6457745962fde73c81a7004bce13cea6992a85c1`, disk SHA-256
`b66ecaa018784d93042d82c33061f6a421ffe5233e29486e1f17b68f27744e9b`, restored
theme `2` and text scale `125`. The USB-only Home screenshot SHA-256 is
`72cafda508f85bdc1a772a81d976f190e0c20c2f9f5d74736b91916bcca8afc8`.

The visible/manual VirtualBox test, guest interaction specifically against
scaled native controls, password authentication, physical hardware and complete
Flux 8 acceptance remain open. PR #58 is still Draft and needs one approval.

## Historical exact-runtime evidence — `7287fe1`

Runtime source: `7287fe1d4f092e146ceda014ebf0a440c1aa4e5f`; identity remains
`7.0.0-dev`. Builds and VM media were created in an isolated worktree so
existing repository images and VM disks were not overwritten.

| Check | Result |
| --- | --- |
| `HOST_TEST_DIR=build/tests/text-scale-exact-host bash scripts/run-host-tests.sh` | PASS — full host regression |
| `HOST_TEST_DIR=build/tests/text-scale-exact-ubsan bash scripts/test-core-ubsan.sh` | PASS — 11 groups |
| `bash scripts/build-linux.sh --configuration debug --rebuild --no-image` | PASS — kernel and userspace Debug |
| `bash scripts/build-installer-linux.sh --configuration release --rebuild` | PASS — Release installer and ISO |
| `python3 scripts/qualification/installer-qemu.py --iso dist/KuroganeOS-7.0.0-dev-x86_64.iso --output /tmp/kuro-text-scale-qemu-7287fe1d --timeout 240 --desktop-lifecycle` | PASS — read-only recovery, safe cancellation, install and two disk boots |
| QEMU Settings interaction and persistent text scale | PASS — changed to `125%`, restored as `125%` after reboot |
| QEMU/OVMF setup ISO Try, no system disk, no PS/2, USB keyboard/tablet, login and Home | PASS — 0 block devices, USB HID keyboard/pointer and Home frame |
| ISO SHA-256 | `cf1f0c0e7d21ccb3c92368508a1f16096310a3113e08815adaef814d7266be80` |
| GitHub Actions `reliable-file`, run [41348763286](https://github.com/KrzysioYT/KuroganeOS/actions/runs/41348763286) | PASS |
| GitHub Actions CLA, run [41349152187](https://github.com/KrzysioYT/KuroganeOS/actions/runs/41349152187) | PASS |
| GitHub Actions `iso-qualification`, run [41348763424](https://github.com/KrzysioYT/KuroganeOS/actions/runs/41348763424) | PASS — ABI/SDK, host regression, IMG/ISO, FAT32/VFS, ISO x20, OVMF and E1000/PCnet/VirtIO NAT |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\smoke-virtualbox-try.ps1 -Iso <ISO-with-hash-above> -TimeoutSeconds 180` | PASS — Try/Login/Desktop, headless PS/2 |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\smoke-virtualbox-iso.ps1 -Iso <ISO-with-hash-above> -TimeoutSeconds 180` | PASS — EFI/AHCI install, ISO detach, disk-only boot, FAT32/PID 1 and PCnet NAT/DHCP/gateway/DNS |
| Visible/manual VirtualBox mouse and keyboard interaction | UNVERIFIED — automated headless input is not a substitute |

The separate USB-only setup-media run used this command against the ISO hash
above; there was no `--disk` or PS/2 device:

```bash
bash scripts/smoke-uefi-iso-qemu.sh <ISO-with-hash-above> \
  --smp 4 --nic none --no-ps2 --usb-keyboard --usb-tablet --timeout 240 \
  --log-dir /tmp/kuro-text-scale-usb-only-qual-7287fe1 \
  --send-key-after-marker-count 'installer: select setup mode:' 1 ret \
  --send-key-after-marker-count '[TEST] red_flux_login_surface: PASS' 1 f12 \
  --click-after-marker '[TEST] red_flux_login_surface: PASS' 320 240 \
  --send-key-after-marker-count '[TEST] live_login_profile: PASS' 1 ret \
  --require-marker '[TEST] hardware_boot_policy: PASS' \
  --require-marker '[TEST] ps2_keyboard: SKIP (optional backend unavailable)' \
  --require-marker '[TEST] ps2_mouse: SKIP (optional backend unavailable)' \
  --require-marker '[TEST] xhci_keyboard_enumeration: PASS' \
  --require-marker '[TEST] xhci_report_pointer_enumeration: PASS' \
  --require-marker '[TEST] xhci_multi_hid_enumeration: PASS' \
  --require-marker '[TEST] usb_hid_keyboard_input: PASS' \
  --require-marker '[TEST] usb_hid_report_pointer_input: PASS' \
  --require-marker '[TEST] red_flux_login_to_desktop: PASS' \
  --require-marker '[SESSION] flux_home_first_frame: PASS' \
  --screendump-after-marker '[SESSION] flux_home_first_frame: PASS' \
  /tmp/kuro-text-scale-usb-only-qual-7287fe1/home.ppm
```

The QEMU result manifest records exact source SHA
`7287fe1d4f092e146ceda014ebf0a440c1aa4e5f`, ISO hash above, restored theme
`2`, restored text scale `125`, and `result: PASS`. Text size is a global
rendering preference, not whole-UI HiDPI or per-user isolation. Automated
VirtualBox 7.2.20 Try and EFI/AHCI install/disk-only reboot passed on this exact
ISO; the scripts used a new temporary VM and VDI. Visible/manual mouse and
keyboard interaction, password authentication, physical hardware and complete
Flux 8 acceptance remain unverified. PR #58 remains Draft and needs one approval.

The USB-only Try run used the same ISO hash above, attached no system disk and
disabled PS/2. QEMU reported `installer block devices: 0`; xHCI keyboard and
report-pointer input reached the live login and Home first frame. The Home
screenshot SHA-256 is `72cafda508f85bdc1a772a81d976f190e0c20c2f9f5d74736b91916bcca8afc8`.

## Historical exact-SHA evidence — `5c319946` (2026-11-01)

Source: `5c3199467b0a1cf726761dadd1ad66c782fa99ae`; runtime identity:
`7.0.0-dev`. This is the runtime fix commit in published PR #58; later
documentation-only commits do not change the tested system payload.

| Check | Result |
| --- | --- |
| `bash scripts/run-host-tests.sh` | PASS — full host regression |
| `HOST_TEST_DIR=build/tests/readonly-fix-ubsan bash scripts/test-core-ubsan.sh` | PASS — 11 groups |
| `bash scripts/build-installer-linux.sh --configuration release` | PASS — SDK and release media build |
| ISO structure validation | PASS — 20 checks |
| QEMU/OVMF `installer-qemu.py --desktop-lifecycle` | PASS — read-only recovery; cancellation preserved blank disk; install; first and second installed boots; authenticated Home; theme `2` restored |
| GitHub Actions `reliable-file`, run [41337504861](https://github.com/KrzysioYT/KuroganeOS/actions/runs/41337504861) | PASS |
| GitHub Actions `iso-qualification`, run [41337504951](https://github.com/KrzysioYT/KuroganeOS/actions/runs/41337504951) | PASS |
| GitHub Actions CLA, run `41337504885` | PASS |
| Debug host/kernel profile | NOT RUN on this SHA |
| VirtualBox on this exact SHA; visible/manual mouse and keyboard | UNVERIFIED |

ISO SHA-256: `b50362b48544f26db7b321fcae43a32e3dfe0e795f50e5fc31035372e9811015`.
QEMU result manifest records this hash, exact source SHA
`5c3199467b0a1cf726761dadd1ad66c782fa99ae`, and `result: PASS`.
VirtualBox 7.2.20 automated Try and EFI/AHCI installation were previously
verified on PR SHA `0c708fea`, not this image; those results are not transferred.
Password authentication, physical hardware, old-image recovery and full Flux 8
acceptance remain unverified. PR #58 remains Draft and needs one approval.

The following checkpoint is historical; its results apply only to its listed
SHAs and artifacts.

## Historical exact-SHA evidence — `ac0d1220046d` (2026-11-01)

Source: `ac0d1220046d35b194e2a6a38de8135258232522`; runtime identity:
`7.0.0-dev`.

| Check | Result |
| --- | --- |
| `bash scripts/run-host-tests.sh` | PASS |
| `HOST_TEST_DIR=build/tests/flux8-ubsan-exact bash scripts/test-core-ubsan.sh` | PASS — 11 groups |
| Full host suite with `-O0 -g3` compiler wrappers | PASS — Debug profile on test commit `7c2171a1` |
| Full host suite (`-O2`) | PASS — Release profile on test commit `7c2171a1` |
| Core UBSan suite | PASS — 11 groups on test commit `7c2171a1` |
| Debug and Release kernel builds | PASS |
| QEMU/OVMF setup-media Try, no disk, USB keyboard/tablet | PASS |
| QEMU/OVMF blank-disk cancellation hash and installation | PASS |
| Installed disk-only boots, account profile, Home and theme persistence | PASS — two boots; theme value 2 restored |
| QEMU USB window lifecycle | PASS — real Terminal/Files PIDs, focus, minimize/restore, maximize, move, resize, close/relaunch |
| GitHub Actions run [41324279554](https://github.com/KrzysioYT/KuroganeOS/actions/runs/41324279554) | PASS — host/ABI/SDK, Linux IMG/ISO, FAT32/VFS, ISO structure x20, OVMF and E1000/PCnet/VirtIO NAT |
| GitHub Actions run [41331938452](https://github.com/KrzysioYT/KuroganeOS/actions/runs/41331938452) | PASS — test/harness commit `7c2171a1`; host/ABI/SDK, full regression, IMG/ISO, FAT32/VFS, ISO x20, OVMF and three NAT profiles |
| VirtualBox 7.2.20 Try → Login → Desktop (`smoke-virtualbox-try.ps1`) | PASS — headless scripted PS/2 input |
| VirtualBox EFI/AHCI install and disk-only boot (`smoke-virtualbox-iso.ps1`) | PASS — fresh temporary VDI, FAT32 root, PID 1 and PCnet NAT/DHCP/gateway/DNS |
| Manual visible VirtualBox mouse/keyboard interaction | UNVERIFIED — still required; headless scripted checks are not a substitute |

ISO SHA-256: `13d0b1c445aadfbc1a075cec4fb97d4a69487bcae8a6214bf0f2735cc722a8f6`.
The installed profile had no password. This does not qualify password
authentication, an old installed image, physical hardware, crash recovery or
the complete 8.0 matrix. The VirtualBox smoke scripts remove their temporary
VMs/disks and logs after completion; their command output recorded PASS.

The release image and runtime checks ran in a fresh exact-SHA scratch worktree;
do not run the builder over existing `dist` or `kurogane.iso` media.

```bash
bash scripts/build-installer-linux.sh --configuration release
python3 scripts/qualification/installer-qemu.py \
  --iso dist/KuroganeOS-7.0.0-dev-x86_64.iso \
  --output build/tests/ac0d1220-installer-qemu --timeout 240 \
  --desktop-lifecycle
bash scripts/smoke-uefi-iso-qemu.sh \
  dist/KuroganeOS-7.0.0-dev-x86_64.iso --smp 4 --nic none --no-ps2 \
  --usb-keyboard --usb-tablet --timeout 180 \
  --log-dir build/tests/ac0d1220-live-try \
  --send-key-after-marker 'installer: select setup mode:' ret \
  --send-key-after-marker-count '[TEST] live_login_profile: PASS' 1 ret \
  --require-marker '[TEST] live_login_profile: PASS' \
  --require-marker '[SESSION] flux_home_first_frame: PASS'
```

All three commands completed with PASS at the source SHA above. The additional
Debug host suite used `-O0 -g3` compiler wrappers; the Release suite used the
runner's `-O2` defaults. Both passed on test commit
`7c2171a1ca5e985334276a955e3ceaf879748d92`.

## Hosted tests

```powershell
wsl.exe bash -lc "cd /mnt/d/KuroganeOS && bash scripts/test.sh"
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
  --output build/tests/flux-8-installed --desktop-lifecycle --password-profile \
  --timeout 240
```

The output directory must not exist. The runner creates its own 512 MiB raw
file with exclusive creation; it never accepts an existing target disk. Only
that file and the read-only ISO are attached during installation. Real USB
input chooses Install, the English/user profile, and the sole test disk.
It first cancels at the erase prompt and verifies the entire
blank-disk hash is unchanged, then confirms INSTALL and requires the guest's
payload verification and completion marker.

The installer VM stops before two separate disk-only boots. Both must reach
an authenticated Home; the first must finish the first-boot transaction and
the second must report persisted installation state. The runner rejects
fallback to live media, guest FAIL markers and failed QEMU exits. It retains
serial logs, screenshots, the disk and a manifest with ISO/disk/harness hashes.
CI uploads evidence without the writable disk images. The password option
uses a public test fixture, rejects a wrong password on the first boot, then
requires the right password before session creation and the completed Login
handoff. It also boots a disposable copy with both account files removed;
Login must reject Enter while the installation marker remains. Without
the option, the runner uses the default passwordless profile.
This is an emulated AHCI test, not an upgrade, power-loss or physical-install
gate. The fixture uses the existing development credential scheme, not a
production password KDF.

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
by Settings with the value restored and applied by Home after reboot. It also
requires a real directory navigation from a click at the last pixel of a
Files tile at 200% UI scale, and restores 200% on the next boot.

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

### Settings viewport at 200%

Settings now measures the caller-owned native content area in logical pixels.
The same row geometry governs drawing, pointer hit-testing and keyboard focus.
The optimized and UBSan libui tests exercise a mixed-height form, a clipped
selection, scroll to the last control, and a reduced viewport. The installed
QEMU lifecycle scenario also shrinks Settings at 200% and requires a fresh
keyboard-focus marker for a control that fits in the new viewport. This is
scoped to Settings; it does not qualify the layout of all desktop apps.

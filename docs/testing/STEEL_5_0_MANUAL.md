# KuroganeOS 5.0 Steel — manual validation

This checklist is for external validation after the automated Steel milestone
qualified on 2026-10-07.

## Authoritative automated baseline

Runtime candidate:

`90d5ffc80f91236e10118324c8e3b5d6f4a1c781`

Merged into `gpt/road-to-15-consolidation` through PR #45 as:

`8374b6ba66a8bb9a4d4ddf8f969816dc789a5be4`

The required Steel Closeout, HPET, SMP, HDA, NVMe, USB Mass Storage, USB
Keyboard and USB Mouse GitHub Actions gates passed for the exact candidate.
Manual VirtualBox/physical-machine testing is additional evidence and must not
be recorded as PASS until it actually succeeds.

## 1. Build a fresh image on Windows

Do not reuse an older ISO from `dist/`.

```powershell
git fetch origin
git checkout gpt/road-to-15-consolidation
git pull --ff-only

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\build-media.ps1 `
  -Configuration release `
  -Rebuild
```

Expected ISO:

```text
dist/KuroganeOS-5.0.0-dev-x86_64.iso
```

Keep `dist/SHA256SUMS.txt` together with the tested ISO.

## 2. Run the automated Oracle VirtualBox smoke

Oracle VirtualBox must be installed and `VBoxManage.exe` must be available.

```powershell
.\scripts\smoke-virtualbox-iso.ps1 `
  -Iso ".\dist\KuroganeOS-5.0.0-dev-x86_64.iso" `
  -TimeoutSeconds 180
```

A complete PASS must include install, power cycle, boot from the installed VDI,
persistent root mount, PID 1/userspace startup and working VirtualBox NAT
networking. A boot to Red Flux Setup alone is not a full PASS.

## 3. Interactive VirtualBox test

Create a separate VM so interactive testing does not interfere with the
temporary smoke VM.

```powershell
.\scripts\create-virtualbox-vm.ps1 `
  -Iso ".\dist\KuroganeOS-5.0.0-dev-x86_64.iso" `
  -Name "KuroganeOS-5.0-Steel-Test" `
  -Start
```

Reference configuration:

```text
UEFI/EFI64:       ON
Secure Boot:      OFF
I/O APIC:         ON
RAM:              2048 MiB or more
CPU:              4 for the SMP pass
Graphics:         VMSVGA, 128 MiB, 3D OFF
Disk:             SATA / Intel AHCI
DVD:              IDE / PIIX4
Network:          NAT
NIC:              PCnet-FAST III for the canonical VBox profile
Audio:            Intel AC'97
Serial:           COM1 -> file
```

## 4. Required manual checks

Record PASS/FAIL separately for every item.

- ISO reaches Red Flux Setup without a fatal diagnostic.
- TRY mode reaches Login/Home.
- INSTALL completes on a blank VDI.
- Installed VDI boots after the ISO is detached.
- A second boot preserves the installed root/files.
- Keyboard input works through setup, login and shell.
- Mouse movement/buttons work in the GUI.
- Network obtains DHCP and DNS works.
- Existing HTTP/HTTPS/network paths do not regress.
- Audio output works on the VirtualBox AC'97 profile.
- `reboot` returns through firmware/platform reset behavior.
- `poweroff` powers the VM off instead of hanging.
- With 4 vCPUs, serial output contains successful AP startup, cross-CPU work
  and TLB shootdown markers.
- No `PANIC_BEGIN`, `FATAL ERROR` or unexpected kernel exception appears.

Useful 5.0 serial markers include:

```text
[TEST] acpi_madt: PASS
[TEST] acpi_power_discovery: PASS
[TEST] hpet_main_counter: PASS
[TEST] smp_ap_startup: PASS
[TEST] smp_cross_cpu_work: PASS
[TEST] smp_tlb_shootdown: PASS
[TEST] red_flux_login_surface: PASS
```

A VirtualBox-specific driver may legitimately use a different backend from the
QEMU qualification matrix; do not invent a PASS marker that the guest did not
emit.

## 5. Optional hardware expansion

After the canonical VirtualBox pass, physical-machine testing should be logged
separately for:

- UEFI boot and GOP framebuffer;
- SATA/AHCI storage;
- NVMe storage;
- USB keyboard and mouse;
- USB Mass Storage;
- Intel HDA audio;
- SMP on more than one logical CPU;
- reboot and ACPI S5 poweroff.

Never run destructive storage tests on a disk containing valuable data. Use a
dedicated empty test disk or removable device.

## 6. Result template

Paste this block into the next development report:

```text
KuroganeOS 5.0 Steel manual validation
ISO: KuroganeOS-5.0.0-dev-x86_64.iso
SHA256:
Host:
VirtualBox version:
VM CPUs:
VM RAM:

Automated VBox smoke: PASS / FAIL
Setup boot: PASS / FAIL
Try -> Login/Home: PASS / FAIL
Install: PASS / FAIL
Installed reboot: PASS / FAIL
Persistence second boot: PASS / FAIL
Keyboard: PASS / FAIL
Mouse: PASS / FAIL
DHCP/DNS/network: PASS / FAIL
Audio: PASS / FAIL
reboot command: PASS / FAIL
poweroff command: PASS / FAIL
4-vCPU SMP markers: PASS / FAIL
Fatal/Panic seen: YES / NO

Serial log:
Notes:
```

Any FAIL is actionable evidence. Attach or paste the serial log around the first
failure marker; fixes should be made against the 5.0 baseline before dependent
6.0 work assumes that subsystem is sound.

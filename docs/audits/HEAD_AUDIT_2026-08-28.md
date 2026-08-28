# HEAD Audit — 2026-08-28

Audited repository: `KrzysioYT/KuroganeOS`
Audited commit: `17bd55091c63544b9585840192f0eb288e9cffff`
Reported version source: `3.3.3-dev` / `DEV BETA`

This audit distinguishes code presence from runtime qualification. It does not upgrade a feature to SUPPORTED merely because code or documentation exists.

## Executive findings

1. The planned baseline is correct: `common/version.h` is still `3.3.3-dev`.
2. HEAD is materially ahead of a simple 3.3.3 snapshot: the latest merge includes active TCP/TLS transport changes and retry/backpressure fixes.
3. Current documentation is inconsistent: `README.md` and `CURRENT_LIMITATIONS.md` describe 3.3.3-dev, while `docs/BUILD_STATUS.md` still identifies 3.3.1-dev as the current line.
4. The existing real VirtualBox smoke already covers a substantial installer path: EFI64 VM, SATA/IntelAHCI VDI, ISO install, ISO detach, installed-disk boot, persistent root, PID 1 and network checks.
5. The current VirtualBox smoke does not independently qualify the required `ISO -> Try -> Login -> Desktop` path. That is the first implementation gap for 3.3.4-dev.
6. The connected execution environment cannot run Oracle VirtualBox, so VirtualBox runtime results remain PENDING until a real x86-64 host executes the acceptance harness.
7. Security debt remains explicit: installed-password verification still uses `FNV1A64-DEV`; it must not be described as secure.
8. Graphics remain software-based. No Direct3D 9/10/11/12 or production GPU acceleration may be claimed.

## Subsystem audit

| Subsystem | Evidence/status at audited HEAD | Qualification state |
|---|---|---|
| Boot / UEFI | native x86-64 `BOOTX64.EFI`, boot protocol v3, EFI ISO tooling | IMPLEMENTED; QEMU/OVMF qualification exists; final VirtualBox 3.3.4 acceptance pending |
| Kernel core | native kernel with IDT/GDT/TSS/IST and subsystem initialization documented in current tree | IMPLEMENTED / DEV |
| Memory | PMM/VMM foundation exists; no demand paging/COW/swap; future SMP hardening remains | PARTIAL |
| Scheduler | PIT-preemptive process scheduling exists; no SMP scheduler yet | IMPLEMENTED single-CPU / NOT SMP QUALIFIED |
| Ring 3 / ELF64 | Ring-3 userspace, ELF64 process launch, PID/TID and PID 1 init exist | IMPLEMENTED |
| Processes | spawn/wait/exit and session supervision exist | IMPLEMENTED / DEV |
| Syscalls | current native syscall ABI exists and is experimental | IMPLEMENTED / NOT ABI-STABLE |
| IPC / shared memory | present in current generation per project docs; deeper 2.0 redesign is later roadmap work | IMPLEMENTED FOUNDATION / DEV |
| VFS / FAT32 | writable FAT32/VFS, persistent root and public file ABI are documented; live package root remains read-only | IMPLEMENTED / DEV |
| AHCI / GPT | real AHCI storage and GPT installer path exist | IMPLEMENTED; broader stress/real-hardware qualification pending |
| PCI | enumeration/device support exists | IMPLEMENTED FOUNDATION |
| ACPI / APIC | MADT/APIC discovery exists; broad ACPI/SMP qualification remains later work | PARTIAL |
| NVMe | not a production-equivalent storage backend | NOT QUALIFIED |
| USB / xHCI | not yet stable/qualified for target 15.0 requirements | PARTIAL / NOT QUALIFIED |
| Networking drivers | E1000 is documented; recent code/CI also exercises PCnet and VirtIO-net paths in QEMU | IMPLEMENTED for selected virtual NICs / DEV |
| IPv4/UDP/DHCP/DNS | native stack exists and CI workflow contains NAT qualification gates | IMPLEMENTED / DEV |
| TCP | real client transport exists; latest HEAD contains retransmit/backpressure/reset handling changes | IMPLEMENTED / ACTIVE STABILIZATION |
| TLS | real integration work exists, but current HEAD commit message still identifies a remaining CLOSE-WAIT/TLS failure under audit | PARTIAL / NOT QUALIFIED |
| Audio | Intel ICH AC'97 kernel PCM backend and bounded Ring-3 playback exist | IMPLEMENTED for AC'97 profile / DEV; HDA absent |
| Graphics | software framebuffer/backbuffer, WindowManager and damage-style scanout | IMPLEMENTED SOFTWARE PATH; hardware acceleration UNSUPPORTED |
| GUI / Red Flux | Login, launcher/Home, Dock and Ring-3 apps exist | IMPLEMENTED / DEV |
| Installer | real GPT/FAT32 installer with language/profile/password-dev flow and destructive confirmation | IMPLEMENTED / ACTIVE RELIABILITY WORK |
| Persistence | writable installed root exists; persistence tests/tools exist | IMPLEMENTED / requires final 3.3.5 hardening |
| Userspace apps | terminal/files/performance/browser/sysmon/settings/about launcher entries exist | IMPLEMENTED subset / DEV |
| Build | Windows/WSL, macOS and Linux tooling documented | IMPLEMENTED; host-specific qualification varies |
| CI/tests | GitHub workflow runs kernel test config, host ABI/SDK tests, full host regressions, media build, FAT32 image validation, ISO structure verification and QEMU/OVMF smoke/network tests | AUTOMATED CI COVERAGE PRESENT |
| VirtualBox | helper and real PowerShell smoke exist | TOOLING IMPLEMENTED; required Try/Login/Desktop gate missing; real-host acceptance PENDING |
| Security | Ring-3 boundary exists, but final process isolation/capabilities/permissions/secure credential model are not release-grade | PARTIAL; security debt must remain explicit |
| Documentation | substantial current docs exist but `BUILD_STATUS.md` is stale relative to version header | NEEDS CORRECTION |

## 3.3.4-dev release-gap analysis

Roadmap requirements:

- ISO -> UEFI boot;
- ISO -> Try;
- Try -> Login;
- Login -> Desktop;
- real VirtualBox boot;
- SATA VDI;
- Install;
- reboot without ISO;
- installed-system boot;
- smoke tests and serial markers;
- regression fixes.

Already represented in existing tooling/code:

- EFI64 VirtualBox VM creation;
- SATA/IntelAHCI VDI;
- ISO boot/install driver;
- deterministic installer input;
- installer completion marker;
- ISO detach;
- disk-first reboot;
- persistent root/PID 1/network markers.

Missing release gate:

- a dedicated real VirtualBox live-session acceptance path that chooses Try, waits for live Login, activates the no-password live profile and proves the Red Flux desktop launcher/session became live.

This missing gate is the first implementation task on `dev/road-to-15`.

## Immediate documentation correction

`docs/BUILD_STATUS.md` must stop describing 3.3.1-dev as current. Until 3.3.4 runtime acceptance is recorded, HEAD remains 3.3.3-dev with target 3.3.4-dev and VirtualBox qualification in progress.

## Release integrity rule

Do not change `KUROGANE_VERSION_STRING` to `3.3.4-dev` and do not create `v3.3.4-dev` until the final candidate passes automated regression/build gates and a real Oracle VirtualBox x86-64 host records the required runtime path. If that host is unavailable, keep the gate PENDING and continue only work that does not require falsely closing the release.

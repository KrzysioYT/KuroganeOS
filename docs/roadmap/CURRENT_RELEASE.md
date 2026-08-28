# Current release

Current:
`3.3.3-dev`

Target:
`3.3.4-dev`

Status:
`QUALIFICATION`

Baseline commit:
`17bd55091c63544b9585840192f0eb288e9cffff`

Candidate commit:
`662eae4fc1f2af85c8c74322e4b8863236a202b1`

Last audit:
`2026-08-29`

## Completed

- [x] Version source verified: `common/version.h` still reports `3.3.3-dev` / `DEV BETA`; no premature release bump was made.
- [x] x86-64 UEFI boot architecture exists.
- [x] Ring-3 ELF64 process/userspace path exists.
- [x] AHCI + GPT + writable FAT32/VFS persistent-root path exists.
- [x] Try/Install setup flow exists.
- [x] Login and Red Flux desktop session gate exist.
- [x] Added a real Oracle VirtualBox `ISO -> Try -> Login -> Desktop` smoke harness driven through guest PS/2 scan codes and serial markers.
- [x] Existing VirtualBox install qualification covers a real SATA/IntelAHCI VDI, install, ISO detach and installed-system reboot.
- [x] Added recoverable installer state-file replacement instead of destructive `unlink -> create -> write` for `/etc/locale.cfg`, `/etc/user.cfg` and `/etc/first.run`.
- [x] Added deterministic fault-injection regression tests for staging, publish, sync and recovery failure paths.
- [x] Added production KuroganeOS FAT32 integration coverage for recoverable state replacement.
- [x] GitHub Actions run `33216094295` / qualification run `509` passed on candidate `662eae4f`.
- [x] Kernel test configuration build passed.
- [x] Host ABI/SDK regression passed.
- [x] Full host regression suite passed.
- [x] Linux IMG/ISO media build passed.
- [x] Production FAT32/VFS image validation passed.
- [x] 20-pass ISO structure verification passed.
- [x] OVMF/QEMU ISO boot passed.
- [x] QEMU E1000 NAT qualification passed.
- [x] QEMU PCnet NAT qualification passed.
- [x] QEMU VirtIO-net NAT qualification passed.
- [x] Installer reliable-file workflow passed with fake-backend fault injection and production FAT32 integration.

## Remaining for 3.3.4-dev

- [ ] Run `ISO -> Try -> Login -> Desktop` on a real x86-64 Oracle VirtualBox host using the candidate commit.
- [ ] Run `ISO -> Install -> SATA VDI -> detach ISO -> installed-system boot` on the same final candidate.
- [ ] Record final VirtualBox serial acceptance evidence.
- [ ] Bump version to `3.3.4-dev` only after the real VirtualBox release gate passes.
- [ ] Freeze release notes and create immutable tag `v3.3.4-dev` only after release closeout.

## Blocked

- [ ] `PENDING REAL VIRTUALBOX HOST ACCEPTANCE`: this connected development environment cannot execute Oracle VirtualBox. This blocks only release closure; independent later development may continue on stacked branches.

## Tests

PASS:
- Audit and baseline verification.
- PowerShell qualification-script parser gate.
- Kernel test build.
- Host ABI/SDK regression.
- Full host regression suite.
- Installer reliable-file fault-injection regression.
- Production FAT32 reliable-file integration regression.
- Linux media build.
- FAT32/VFS image validation.
- 20-pass ISO verifier.
- OVMF/QEMU boot.
- QEMU E1000/PCnet/VirtIO-net NAT qualification.

FAIL:
- None known on candidate `662eae4f` from automated qualification.

PENDING:
- Oracle VirtualBox Try/Login/Desktop runtime acceptance.
- Oracle VirtualBox final install/reboot runtime acceptance.

Current commit:
`662eae4fc1f2af85c8c74322e4b8863236a202b1`

Next action:
Keep 3.3.4 in `QUALIFICATION`, preserve candidate `662eae4f` for real VirtualBox acceptance, and continue independent 3.3.5 installer-reliability work on a stacked branch without claiming 3.3.4 COMPLETE.

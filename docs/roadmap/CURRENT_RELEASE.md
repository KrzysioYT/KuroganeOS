# Current release

Current:
`3.3.3-dev`

Target:
`3.3.4-dev`

Status:
`QUALIFICATION`

Baseline commit:
`17bd55091c63544b9585840192f0eb288e9cffff`

Last audit:
`2026-08-28`

## Completed

- [x] Version source verified: `common/version.h` reports `3.3.3-dev` / `DEV BETA`.
- [x] HEAD verified before roadmap work: `17bd55091c63544b9585840192f0eb288e9cffff`.
- [x] x86-64 UEFI boot architecture exists.
- [x] Ring-3 ELF64 process/userspace path exists.
- [x] AHCI + GPT + writable FAT32/VFS persistent-root path exists.
- [x] Try/Install setup flow exists.
- [x] Login and Red Flux desktop session gate exist.
- [x] VirtualBox helper creates EFI64 + SATA/IntelAHCI + ISO DVD VM.
- [x] Existing VirtualBox installer smoke creates a real VDI, performs install, detaches ISO and boots installed media.
- [x] Existing automated CI builds media, runs host tests, validates FAT32/VFS image, verifies ISO structure and boots via OVMF/QEMU.
- [x] Current HEAD contains recent TCP/TLS transport fixes rather than a clean pre-network 3.3.3 baseline.
- [x] Master roadmap and version-history tracking established.

## Remaining for 3.3.4-dev

- [ ] Add a real Oracle VirtualBox `ISO -> Try -> Login -> Desktop` smoke gate.
- [ ] Run the new Try path on an x86-64 host with Oracle VirtualBox.
- [ ] Re-run `ISO -> Install -> SATA VDI -> reboot without ISO -> installed system` qualification on the final 3.3.4 candidate.
- [ ] Confirm serial markers for live Login/Desktop and installed-system boot on the same candidate.
- [ ] Confirm no critical regressions in host/QEMU CI.
- [ ] Update release notes and current documentation with measured results.
- [ ] Bump version to `3.3.4-dev` only after qualification gates are satisfied.
- [ ] Create immutable tag `v3.3.4-dev` only after release closeout.

## Blocked

- [ ] `PENDING REAL VIRTUALBOX HOST ACCEPTANCE`: the connected execution environment cannot launch Oracle VirtualBox. This is a qualification blocker, not permission to claim PASS.

## Tests

PASS:
- Version/baseline audit.
- Existing source-level and documentation evidence for UEFI/Ring-3/AHCI/FAT32/installer/login/desktop paths.
- Existing CI workflow contains real host regression, media build, image validation, ISO verifier and OVMF/QEMU boot gates.

FAIL:
- None recorded by this audit yet.

PENDING:
- New branch CI after implementation changes.
- Oracle VirtualBox Try/Login/Desktop runtime acceptance.
- Oracle VirtualBox final install/reboot runtime acceptance for 3.3.4.

Current commit:
`17bd55091c63544b9585840192f0eb288e9cffff` (baseline; this file is updated as commits land)

Next action:
Implement the missing real VirtualBox Try/Login/Desktop qualification harness, run branch CI, fix regressions, and keep 3.3.4 in `QUALIFICATION` until a real VirtualBox host records PASS.

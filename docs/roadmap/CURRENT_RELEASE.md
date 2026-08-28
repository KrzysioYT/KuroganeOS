# Current release

## Formal release gate

Current version:
`3.3.3-dev`

Target version:
`3.3.4-dev`

Generation:
`Red Flux`

Status:
`QUALIFICATION`

Current task:
`Oracle VirtualBox real-host acceptance for 3.3.4-dev`

Baseline commit:
`17bd55091c63544b9585840192f0eb288e9cffff`

3.3.4 candidate commit reserved for real VirtualBox acceptance:
`662eae4fc1f2af85c8c74322e4b8863236a202b1`

Last audit:
`2026-08-29`

## Active stacked development

Active development version:
`3.4.0-dev`

Active branch:
`dev/3.4.0-service-architecture`

Red Flux closeout branch:
`dev/3.3.9-red-flux-closeout`

Combined 3.3.7 + 3.3.8 merge:
`3e1c3a7b9e7ba12281e80b8ef41a415c929ba70d`

Verified 3.3.9 closeout code:
`ee9e1839247600b7882892aff463b4730839c9a3`

Stacked development is not evidence that the earlier versions have been
formally released. No immutable release tag is created while the mandatory
3.3.4 real VirtualBox release gate remains open.

## Completed

### 3.3.4 qualification tooling

- [x] Version source verified; no premature version bump.
- [x] x86-64 UEFI, Ring-3 ELF64, AHCI/GPT/FAT32 persistent-root foundation verified.
- [x] Real Oracle VirtualBox `ISO -> Try -> Login -> Desktop` smoke harness implemented.
- [x] Real Oracle VirtualBox install harness covers SATA/IntelAHCI VDI, install,
      ISO detach and installed-system reboot.
- [x] Automated QEMU/OVMF media, filesystem and network qualification passed on
      the 3.3.4 candidate.

### Stacked 3.3.5 installer reliability

- [x] Recoverable state-file replacement implemented for locale, profile and
      first-run state.
- [x] Deterministic failure/recovery tests implemented.
- [x] Production FAT32 integration test implemented and passed.
- [x] Dedicated reliable-file workflow run `33216094288` passed.

### Stacked 3.3.7 TLS foundation

- [x] Real Mbed TLS handshake path retained; no fake TLS success path introduced.
- [x] X.509 peer certificate retention fixed for post-handshake validation.
- [x] Certificate time validation remains enabled and fail-closed.
- [x] Combined guest HTTPS qualification run `33220761526` passed, including
      `Require real TLS handshake and HTTPS response` on QEMU/E1000.

### Stacked 3.3.8 userspace resource ownership

- [x] Real process file-handle ownership telemetry integrated.
- [x] Runtime cleanup closes active file handles before publishing final count.
- [x] Handle ownership regression run `33220748290` passed.
- [x] Cleanup-ordering regression run `33219888365` passed.

### Stacked 3.3.9 Red Flux closeout

- [x] 3.3.7 TLS and 3.3.8 userspace work combined with a real two-parent merge.
- [x] Combined UEFI ISO qualification run `33220774861` passed.
- [x] IPC channel/event/shared-memory tests were added to the default full host
      regression suite in `ee9e1839...`.
- [x] Expanded UEFI ISO qualification run `33220980716` passed on
      `ee9e1839247600b7882892aff463b4730839c9a3`.
- [x] Expanded run passed kernel build, ABI/SDK, full host suite including the
      IPC tests, media build, FAT32/VFS, 20-pass verifier, OVMF, E1000, PCnet,
      VirtIO-net and artifact publication.

### Stacked 3.4.0 service architecture

- [x] Clean development branch created from the verified Red Flux closeout line.
- [x] Public `kurogane/service.h` foundation implemented over the existing real
      PID-owned named IPC transport; no duplicate fake registry was introduced.
- [x] SDK umbrella header exports the service API.
- [x] ABI regression locks service types/capacities to the underlying IPC ABI.
- [ ] Full 3.4.0 qualification run `33221125505` is still in progress and must
      pass before this atom is marked TESTED/QUALIFIED.

## Remaining

### Formal 3.3.4 release gate

- [ ] Run `ISO -> Try -> Login -> Desktop` on a real x86-64 Oracle VirtualBox host.
- [ ] Run `ISO -> Install -> SATA VDI -> detach ISO -> installed-system boot` on
      the same final candidate.
- [ ] Record final VirtualBox serial acceptance evidence.
- [ ] Only then bump/freeze/tag 3.3.4-dev.

### Active 3.4.0

- [ ] Finish run `33221125505` on service API head `716d2cac...`.
- [ ] On PASS, record the service API atom as TESTED and continue 3.4.0 service
      discovery/registration qualification without creating parallel registry state.

## Blockers

- [ ] `PENDING REAL VIRTUALBOX HOST ACCEPTANCE` — connected CI cannot execute the
      required Oracle VirtualBox real-host release gate. This blocks formal
      3.3.4 closure, not independent stacked development.

## Test results

PASS:
- 3.3.4 automated candidate qualification, including QEMU/OVMF and NAT matrix.
- Installer reliable-file fault injection and production FAT32 integration.
- Combined userspace ownership regression: `33220748290`.
- Combined real guest TLS/HTTPS qualification: `33220761526`.
- Combined full UEFI ISO qualification: `33220774861`.
- Expanded 3.3.9 full qualification with IPC regression coverage: `33220980716`.

FAIL:
- None known on verified 3.3.9 closeout code.

PENDING:
- 3.4.0 full qualification run `33221125505`.
- Oracle VirtualBox Try/Login/Desktop real-host acceptance.
- Oracle VirtualBox install/reboot real-host acceptance.

Last verified commit:
`ee9e1839247600b7882892aff463b4730839c9a3`

Current stacked development commit under qualification:
`716d2cac9c49a27d7e8cf11f813de56b9d29f256`

Next action:
Finish run `33221125505`. If it passes, document 3.4.0 service API as tested and
continue the next smallest service-discovery/registration reliability task over
the existing IPC backend. Keep 3.3.4 in `QUALIFICATION` until real Oracle
VirtualBox acceptance exists.

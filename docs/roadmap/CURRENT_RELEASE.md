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
`3.3.9-dev`

Branch:
`dev/3.3.9-red-flux-closeout`

Combined 3.3.7 + 3.3.8 merge:
`3e1c3a7b9e7ba12281e80b8ef41a415c929ba70d`

Last fully combined automated qualification head:
`61c917e58ebbfc7ad4f0a8ee6d974e6d088a2860`

Current closeout regression code head:
`ee9e1839247600b7882892aff463b4730839c9a3`

Documentation continues after the code head and does not change the qualified
runtime code.

Stacked development is not evidence that 3.3.4, 3.3.5, 3.3.6, 3.3.7, 3.3.8
or 3.3.9 have been formally released. No immutable release tag is created while
the earlier mandatory release gate remains open.

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
- [x] Earlier cleanup-ordering regression run `33219888365` passed.

### Stacked 3.3.9 Red Flux closeout

- [x] 3.3.7 TLS and 3.3.8 userspace work combined with a real two-parent merge.
- [x] Combined UEFI ISO qualification run `33220774861` passed.
- [x] That run passed kernel test build, ABI/SDK tests, full host regression,
      media build, production FAT32/VFS validation, 20-pass ISO verifier,
      OVMF boot, and QEMU NAT with E1000, PCnet and VirtIO-net.
- [x] Existing IPC channel/event/shared-memory tests were identified as missing
      from the default full host regression suite and added in `ee9e1839...`.

## Remaining

### Formal 3.3.4 release gate

- [ ] Run `ISO -> Try -> Login -> Desktop` on a real x86-64 Oracle VirtualBox host.
- [ ] Run `ISO -> Install -> SATA VDI -> detach ISO -> installed-system boot` on
      the same final candidate.
- [ ] Record final VirtualBox serial acceptance evidence.
- [ ] Only then bump/freeze/tag 3.3.4-dev.

### Active 3.3.9 closeout

- [ ] Complete expanded full qualification run `33220980716` for
      `ee9e1839247600b7882892aff463b4730839c9a3`; it additionally exercises IPC
      channel/event/shared-memory in `scripts/test.sh`.
- [ ] If the expanded regression passes, fast-forward the clean
      `dev/3.4.0-service-architecture` branch to the closeout head and begin the
      3.4.0 service registry/discovery work over the existing named IPC backend.

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

FAIL:
- None known on the combined 3.3.9 code at the last completed qualification.

PENDING:
- Expanded closeout regression `33220980716` after adding IPC tests to the
  default host suite.
- Oracle VirtualBox Try/Login/Desktop real-host acceptance.
- Oracle VirtualBox install/reboot real-host acceptance.

Last verified commit:
`61c917e58ebbfc7ad4f0a8ee6d974e6d088a2860`

Current stacked code commit under regression:
`ee9e1839247600b7882892aff463b4730839c9a3`

Next action:
Finish run `33220980716`. On PASS, record `ee9e1839...` as the latest verified
3.3.9 closeout code, fast-forward `dev/3.4.0-service-architecture`, and implement
side-effect-free service discovery plus explicit userspace service API over the
existing PID-owned named IPC registry. Keep 3.3.4 in `QUALIFICATION` until real
Oracle VirtualBox acceptance exists.

# Build status

Data: 29 sierpnia 2026 r.
Formal release candidate: `662eae4fc1f2af85c8c74322e4b8863236a202b1`

## Current stage

KuroganeOS **3.3.3-dev — DEV BETA** pozostaje bieżącym numerem wersji w kodzie.
Następny release, **3.3.4-dev**, pozostaje w fazie `QUALIFICATION` wyłącznie
z powodu wymaganego realnego Oracle VirtualBox acceptance. Kod nie został
sztucznie podbity do 3.3.4-dev i nie istnieje release tag `v3.3.4-dev`.

Niezależna praca 3.3.5–3.3.8 jest prowadzona na stacked branches. Nie oznacza
to zamknięcia ani przeskoczenia release gate 3.3.4.

## Status vocabulary

Dokument rozróżnia:

- `IMPLEMENTED` — istnieje prawdziwa implementacja/backend;
- `TESTED` — istnieje test i został wykonany;
- `QUALIFIED` — wymagany release/środowiskowy gate przeszedł;
- `EXPERIMENTAL` — działa tylko w ograniczonym lub wczesnym zakresie;
- `PENDING` — implementacja może istnieć, ale wymagany gate nie został wykonany;
- `UNSUPPORTED` — brak wspieranej implementacji.

Samo skompilowanie kodu nie oznacza `QUALIFIED`.

## Current working foundation

- x86-64 UEFI `BOOTX64.EFI` / boot protocol v3 — `IMPLEMENTED / TESTED`;
- VMM, GDT/TSS/IST, IDT — `IMPLEMENTED / TESTED`;
- Ring 3, ELF64, PID/TID, spawn/wait/exit, `/system/init` PID 1 — `IMPLEMENTED / TESTED`;
- AHCI, GPT, writable FAT32/VFS persistent root — `IMPLEMENTED / TESTED`;
- Try/Install media — `IMPLEMENTED / TESTED`;
- Red Flux Login/Desktop Ring-3 path — `IMPLEMENTED / TESTED`;
- software framebuffer/backbuffer + GOP presentation — `IMPLEMENTED / TESTED`;
- E1000, PCnet, VirtIO-net paths used by current VM qualification — `IMPLEMENTED / TESTED` in QEMU;
- AC'97 bounded PCM backend — `IMPLEMENTED / TESTED` at current foundation level;
- build tooling Windows/WSL, macOS, Linux x86-64 — `IMPLEMENTED`;
- ISO El Torito EFI + GPT ESP structural verifier — `IMPLEMENTED / TESTED`.

## 3.3.4 VirtualBox qualification

Real Oracle VirtualBox tooling is complete:

```text
ISO -> EFI64
ISO -> Try -> Login -> Red Flux Desktop
ISO -> Install
SATA / IntelAHCI VDI
install
power off
ISO detach
installed-disk boot
persistent FAT32 root
PID 1
network boot markers
```

Status:

```text
VirtualBox qualification tooling: IMPLEMENTED
Try -> Login -> Desktop harness: IMPLEMENTED
Install -> VDI -> reboot harness: IMPLEMENTED
Automated candidate qualification: PASS
Real Oracle VirtualBox host execution: PENDING
Release closure: PENDING
```

GitHub Actions run `33216094295` / qualification run `509` passed on candidate
`662eae4f`, including:

- kernel test build;
- ABI/SDK regression;
- full host regression suite;
- IMG/ISO media build;
- production FAT32/VFS validation;
- 20-pass ISO verifier;
- OVMF/QEMU boot;
- QEMU E1000 NAT;
- QEMU PCnet NAT;
- QEMU VirtIO-net NAT.

This is not a substitute for the required Oracle VirtualBox real-host run.

## Installer reliability — stacked 3.3.5 work

- recoverable state-file transactions for profile/locale/first-run state — `IMPLEMENTED / TESTED`;
- production FAT32 fault/recovery integration — `TESTED`;
- shared installer/Login `FNV1A64-DEV` verifier — `IMPLEMENTED / TESTED`;
- fail-closed installed profile parsing — `IMPLEMENTED / TESTED`;
- good-password acceptance / bad-password rejection — `TESTED`;
- EN/PL profile persistence through sync + reopen/remount-style path — `TESTED`;
- secure password KDF — `UNSUPPORTED` in this generation; `FNV1A64-DEV` remains explicitly development-only.

## Network stabilization — stacked 3.3.6 work

- TCP graceful close no longer reports success after a failed FIN transmission — `IMPLEMENTED / TESTED`;
- bounded graceful teardown / CLOSE-WAIT handling — `IMPLEMENTED / TESTED`;
- explicit reset/abort fallback — `IMPLEMENTED / TESTED`;
- existing TCP regression suite after the fix — `PASS`.

## TLS foundation — stacked 3.3.7 work

- production DNS -> TCP/443 -> Mbed TLS path — `IMPLEMENTED / TESTED`;
- entropy/CTR_DRBG — `IMPLEMENTED / TESTED` in the qualified positive path;
- CA PEM parsing — `IMPLEMENTED / OBSERVED IN GUEST`;
- SNI and required certificate verification — `IMPLEMENTED / TESTED` in the qualified positive path;
- explicit certificate validity interval check against KuroganeOS wall time — `IMPLEMENTED / TESTED`;
- real HTTPS guest qualification on QEMU/OVMF E1000 — `QUALIFIED`.

The TLS gate intentionally failed while security prerequisites were incomplete.
Those failures exposed two real issues instead of being suppressed:

1. transient CMOS snapshot instability during certificate-time validation;
2. Mbed TLS peer certificate retention was disabled while KuroganeOS required
   `mbedtls_ssl_get_peer_cert()` for a post-handshake validity check.

RTC snapshot handling was hardened and transient raw CMOS failures can be bridged
only from a previously verified hardware RTC snapshot using monotonic PIT time
inside a bounded window. Certificate expiry checks were not bypassed.

Commit `626a1fb3eb34fedbbfe21d7c11e32570c763fab6` enables
`MBEDTLS_SSL_KEEP_PEER_CERTIFICATE` so the already-verified peer certificate is
available to the explicit post-handshake check.

Workflow run `33219821941` completed successfully on that commit. Runtime evidence:

```text
[uefi-qemu] disk/e1000 DHCP/gateway network: PASS
[uefi-qemu] disk/e1000 real TLS/HTTPS handshake: PASS
```

Scope matters: this qualifies the current QEMU/OVMF E1000 positive HTTPS path.
Oracle VirtualBox, real hardware, negative certificate cases and broader TLS
stress remain `PENDING`.

## Userspace I/O ownership — stacked 3.3.8 work

- `process::Stat.handle_count` is backed by the real runtime file-handle table — `IMPLEMENTED / TESTED`;
- open/close operations synchronize ownership count — `IMPLEMENTED / TESTED`;
- runtime cleanup closes active file handles, clears slots, then publishes final count — `IMPLEMENTED / TESTED`;
- repeatable process/handle regression + full test-kernel compile — `PASS` in run `33219449804`;
- close-before-accounting cleanup-order regression + full kernel compile — `PASS` in run `33219888365`;
- complete fault/forced-termination end-to-end resource-lifetime qualification — `PENDING`.

## Known gaps / DEV warnings

- `FNV1A64-DEV` is not a secure password KDF;
- no final users/groups/ACL/capability security model;
- no qualified SMP/SMP-aware scheduler;
- NVMe is not a production-qualified storage backend;
- USB/xHCI is not yet release-qualified;
- Intel HDA is not the qualified primary audio backend;
- userspace networking is not yet the target async service/socket architecture;
- TLS QEMU/E1000 positive HTTPS path is qualified, but TLS 1.3, broad CA coverage, negative-certificate matrix, VirtualBox and real-hardware TLS remain incomplete;
- no Direct3D 9/10/11/12 compatibility layer and no qualified hardware GPU acceleration;
- real-hardware qualification is incomplete;
- no final recovery environment or transactional system updater.

## Source of truth

Release checkpoint: `docs/roadmap/CURRENT_RELEASE.md`

Roadmap to 15.0.0: `docs/roadmap/MASTER_ROADMAP_15.md`

Limitations: `docs/CURRENT_LIMITATIONS.md`

Baseline audit: `docs/audits/HEAD_AUDIT_2026-08-28.md`

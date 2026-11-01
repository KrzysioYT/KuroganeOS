# KuroganeOS — Deferred Test Backlog

Per the Road to 15 development policy adopted on 2026-08-30, implementation proceeds with compile/build gates while runtime and regression verification is collected here for a later single qualification pass.

## Current exact-SHA qualification checkpoint — `5c319946` (2026-11-01)

Source commit `5c3199467b0a1cf726761dadd1ad66c782fa99ae`; runtime
`7.0.0-dev`; published PR #58 head.

- PASS: full host regression and all 11 core UBSan groups.
- PASS: Release build, SDK build, and 20 ISO structure validations.
- PASS: QEMU/OVMF read-only-disk recovery, nondestructive install cancellation,
  fresh installation, two installed-disk boots, authenticated Home and theme
  restoration (`2`).
- PASS: Actions `reliable-file` `41337504861`, `iso-qualification` `41337504951`
  and CLA `41337504885`, all on this exact head.
- ISO SHA-256: `b50362b48544f26db7b321fcae43a32e3dfe0e795f50e5fc31035372e9811015`.
- NOT RUN: Debug host/kernel profile on this exact head.
- UNVERIFIED: VirtualBox on this exact image and visible/manual mouse/keyboard;
  automated VirtualBox evidence is from prior SHA `0c708fea`. Password
  authentication, physical hardware, old-image recovery, crash/recovery and
  the remaining Flux 8 matrix are also unverified.

## Historical exact-SHA qualification checkpoint — `ac0d122` (2026-11-01)

Source commit `ac0d1220046d35b194e2a6a38de8135258232522`; runtime
`7.0.0-dev`.

The test-runner-only follow-up is `7c2171a1ca5e985334276a955e3ceaf879748d92`;
the installed system payload and ISO are unchanged.

- PASS: full host regression in Release (`-O2`) and Debug (`-O0 -g3`), plus
  all 11 core UBSan groups, on the follow-up test commit.
- PASS: Debug and Release kernel builds.
- PASS: QEMU/OVMF Try without a target disk, install cancellation without
  changing the blank target, installation, two installed boots, default
  no-password account/Home, Terminal/Files window lifecycle, and theme
  restoration after reboot.
- PASS: GitHub Actions run `41324279554` (host, IMG/ISO, FAT32/VFS, ISO x20,
  OVMF, E1000/PCnet/VirtIO NAT).
- PASS: GitHub Actions run `41331938452` on test/harness commit `7c2171a1`
  (host/ABI/SDK, full regression, IMG/ISO, FAT32/VFS, ISO x20, OVMF and three
  NAT profiles).
- PASS: VirtualBox 7.2.20 automated Try/Login/Desktop and EFI/AHCI installation,
  installed-disk boot, FAT32, init, PCnet NAT/DHCP/gateway/DNS on the exact ISO.
- UNVERIFIED: manual visible VirtualBox mouse/keyboard interaction, password
  authentication, old installed-image recovery, physical hardware, complete
  crash/recovery and the full Flux 8 acceptance matrix.
- ISO SHA-256: `13d0b1c445aadfbc1a075cec4fb97d4a69487bcae8a6214bf0f2735cc722a8f6`.

## 3.5 Connected Userspace
- UDP socket host regressions: ownership, stale handles, bind collisions, generation reuse, bounded receive queue, BufferTooSmall, ephemeral bind, process cleanup.
- UDP loopback regressions: self-loopback, cross-process loopback, queue saturation, unused loopback destination, no 127/8 physical-NIC leakage.
- Ring-3 public UDP ABI roundtrip through syscalls 57..62.
- Ring-3 socket cleanup after process exit and immediate port rebind.
- TCP nonblocking progression regressions: begin_connect retry/backpressure, SYN/SYN-ACK progression, bounded try_send window accounting, ACK progression, try_receive, FIN completion, RST/error paths.
- Process-owned TCP socket pool regressions: session exhaustion, PID ownership, protocol-specific bind collisions, async connect retries, partial send accounting, receive EOF, graceful close retry, process-exit cleanup, stale handles.
- Socket readiness regressions: UDP queue/read/write/connect flags, TCP connect/read/write/hangup/error transitions, stale/PID ownership, timeout sleeping behavior; replace tick-probe wait with direct scheduler object wake when waitable-I/O plumbing is available.
- DNS Service regressions: bounded client table, request-id correlation, malformed/unsupported request rejection, async response delivery, IPC backpressure preservation, disconnect cleanup, resolver failure propagation, PID1 supervisor restart.
- TLS userspace integration regressions.
- Network event publisher regressions: broker reconnect, ready/link/address/DNS edge emission, unchanged-state silence, supervisor restart.
- Application registry regressions: manifest discovery, malformed manifest rejection, duplicate IDs, bounded capacity, lookup/index/count IPC, supervisor restart.
- Async Audio Service regressions: multi-client open/submit/gain/close, bounded queue backpressure, PCM saturation mixing, disconnect cleanup, AC97 busy/error recovery, PID1 supervisor restart.

## 3.6 Flux Stabilization
- Damage-region regressions: bounded region capacity/fallback-to-full, clipping/overflow edges, overlap merge, per-window UI_PRESENT invalidation, minimized-window silence, full redraw on geometry/z-order changes, partial GOP span presentation and cursor interaction.

## 4.0 Pre-Steel / Device Model 2.0

Host regression coverage now exercises generation-safe stale handles, resource
bounds, derived capabilities, hot-remove, parent unlinking, slot reuse,
lifecycle generation changes for status/claim/release, attach-failure
isolation, fallback binding, runtime failure cleanup and rebind. Socket host
coverage also checks invalid readiness handles/masks and readiness after TCP
reset; Ring-3 blocking wait still needs a runtime qualification.
Runtime qualification and the remaining lifecycle/driver-manager cases remain
deferred.

The Ring-3 `/system/sockprb` probe and its wake/cleanup workers are now included
in Linux/WSL media. Controlled TCP qualification remains pending until QEMU is
run with echo, close, refused, timeout and reset endpoint services.

- Device Model 2.0 regressions: generation-safe handle stale rejection, slot reuse, active/high-water accounting, hot-remove policy, parent unlinking, child/claimed removal rejection, capability derivation, resource query bounds, lifecycle generation on state/claim/release.

- Ring-3 Device API regressions: active-index enumeration, stale generation handle query, ABI version/size validation, parent handle, capability/state/lifecycle snapshots, resource bounds, no writable MMIO/PIO/DMA mapping side effects.

- Driver Manager 2.0 regressions: probe/attach failure accounting, lifecycle generation, detach on unbind, claim release, attached-count accounting, rebind after failure, runtime failure isolation, invalid/corrupt binding rejection, failure-stage diagnostics.

## 5.0 Steel / Hardware
- PCI capability regressions: status-bit gating, type-0/type-1 headers, bounded linked-list traversal, invalid offset/alignment, cycle detection, PM/MSI/MSI-X/PCIe lookup, MSI control decode, MSI-X table/PBA BIR+offset decode, absent capability behavior.

## Filesystem foundation
- KuroFS v1 metadata core: format/mount on bounded block devices, redundant superblock fallback/disagreement, CRC corruption, geometry overflow/rejection, root inode validation, bitmap metadata reservation, flush/write failure propagation.

## Existing deferred qualification
- Full host regression under separate Debug and Release host-test profiles.
- Remaining filesystem/network/service and fatal-diagnostic regression sweeps.
- Full Flux app, clipboard, accessibility, HiDPI, settings, crash and recovery
  matrix; the exact-SHA QEMU evidence above is scoped to the listed scenarios.
- The 3.3.3-dev notes that treated VirtualBox as optional are historical and do
  not apply to Flux 8.0. Flux 8 requires visible mouse/keyboard acceptance in
  x86-64 VirtualBox on a compatible host; QEMU and headless scripted VBox runs
  are not substitutes. Missing VirtualBox is BLOCKED; the present manual test
  is UNVERIFIED.

## Final pre-15.0 qualification pass
- Execute every deferred item above together with milestone-specific tests accumulated here.
- Do not convert deferred items into PASS markers until they have actually executed successfully.

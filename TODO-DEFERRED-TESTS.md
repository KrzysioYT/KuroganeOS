# KuroganeOS — Deferred Test Backlog

Per the Road to 15 development policy adopted on 2026-08-30, implementation proceeds with compile/build gates while runtime and regression verification is collected here for a later single qualification pass.

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
- Full host regression suite.
- QEMU/OVMF runtime smoke and integration qualification.
- Filesystem/network/service regression sweeps.
- Fatal diagnostic regression sweep.
- VirtualBox remains an optional external target and is not a release gate.

## Final pre-15.0 qualification pass
- Execute every deferred item above together with milestone-specific tests accumulated here.
- Do not convert deferred items into PASS markers until they have actually executed successfully.

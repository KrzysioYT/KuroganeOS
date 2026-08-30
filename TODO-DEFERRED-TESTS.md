# KuroganeOS — Deferred Test Backlog

Per the Road to 15 development policy adopted on 2026-08-30, implementation proceeds with compile/build gates while runtime and regression verification is collected here for a later single qualification pass.

## 3.5 Connected Userspace
- UDP socket host regressions: ownership, stale handles, bind collisions, generation reuse, bounded receive queue, BufferTooSmall, ephemeral bind, process cleanup.
- UDP loopback regressions: self-loopback, cross-process loopback, queue saturation, unused loopback destination, no 127/8 physical-NIC leakage.
- Ring-3 public UDP ABI roundtrip through syscalls 57..62.
- Ring-3 socket cleanup after process exit and immediate port rebind.
- TCP nonblocking progression regressions: begin_connect retry/backpressure, SYN/SYN-ACK progression, bounded try_send window accounting, ACK progression, try_receive, FIN completion, RST/error paths.
- Process-owned TCP socket pool regressions: session exhaustion, PID ownership, protocol-specific bind collisions, async connect retries, partial send accounting, receive EOF, graceful close retry, process-exit cleanup, stale handles.
- DNS/TLS userspace integration regressions.
- Network event publisher regressions: broker reconnect, ready/link/address/DNS edge emission, unchanged-state silence, supervisor restart.

## Existing deferred qualification
- Full host regression suite.
- QEMU/OVMF runtime smoke and integration qualification.
- Filesystem/network/service regression sweeps.
- Fatal diagnostic regression sweep.
- VirtualBox remains an optional external target and is not a release gate.

## Final pre-15.0 qualification pass
- Execute every deferred item above together with milestone-specific tests accumulated here.
- Do not convert deferred items into PASS markers until they have actually executed successfully.

# KuroganeOS — Current Release State

Last updated: 2026-08-30

## CURRENT FORMAL VERSION

**3.3.3-dev — Red Flux**

Status: **QUALIFIED (SCOPED DEV MILESTONE)**

Progress: **100% of the defined Red Flux scope**

The compiled/runtime version string remains `3.3.3-dev`. Road-to-15 milestone qualification is tracked independently from that string; qualifying 3.4 does not silently relabel already-built 3.3.3-dev media.

Red Flux remains qualified for its deliberately bounded UEFI/installer/FAT32/Ring-3/network/TLS/desktop/audio/GOP scope. Later SMP, NVMe parity, hardware GPU acceleration, HDA, final security isolation, package/update/recovery and Forge work remain future milestones.

### Current 3.3.3 evidence

- Fatal Diagnostic deliberate-fault qualification: Actions run `33315953767` — **PASS** after the 3.4 runtime stack-ownership fix.
- Fatal Diagnostic remains kernel-owned, heap-independent after fatal transition, userspace/PNG-independent, with real CPU/process/register state, bounded event history, serial mirror and nested panic fallback.
- Oracle VirtualBox remains **OPTIONAL / EXTERNAL VALIDATION** and is not a milestone percentage or release blocker when the environment is unavailable.

---

## QUALIFIED MILESTONE

**3.4.0-dev — System Services**

Status: **QUALIFIED**

The milestone is closed from real runtime evidence, not from code presence alone.

### System Services qualification status

| Area | Status | Evidence / boundary |
|---|---|---|
| Service Core | QUALIFIED | named registration/unregister/discovery/lookup, PID ownership, metadata and version negotiation |
| Named IPC | QUALIFIED | generation-safe endpoints/connections and process-exit cleanup |
| Event Broker | QUALIFIED | subscribe/unsubscribe/publish/wait/wakeup with Ring-3 scheduling integration |
| Settings Service | QUALIFIED | typed persistent settings, reload and change events |
| Notification Service | QUALIFIED | lifecycle, roundtrip and liveness |
| Account Service | QUALIFIED | service lifecycle, lookup/roundtrip and liveness |
| Session Service | QUALIFIED | owned session lifecycle, Login integration, roundtrip and liveness |
| Clipboard Service | QUALIFIED | bounded state, roundtrip, process ownership, crash/restart/rebind |
| Filesystem public API | QUALIFIED | persistent FAT32/VFS API and Ring-3 filesystem probe |
| Service Recovery | QUALIFIED | stale cleanup, restart/rebind and forced Ring-3 service crash recovery |
| Version Negotiation | QUALIFIED | metadata contract and client/server negotiation |
| Lifecycle Cleanup | QUALIFIED | process spawn/wait/reap, stale-frame retirement and resource release |
| Stress | QUALIFIED | 256-iteration channel churn runtime proof |
| Combined Runtime | QUALIFIED | all required System Services closeout markers under OVMF/QEMU |

### Root-cause closure

The historical closeout panic after `[TEST] filesystem_service_api: PASS` was not a kernel `ud2`. The runtime `RIP=0xA3C01` was outside the loaded PIE kernel (`KERNEL_LOAD_BASE=0x3CF89000`), proving corrupted control flow. The expected saved return normalized to `0x4A678`, immediately after `x86_64_enter_user` in `user::runtime::run()` (`kernel/user/runtime_base.inc:1734`).

Bounded diagnostics then showed `fsprobe` had `saved_return=0` and its saved launch stack sat 0x198 bytes inside the old 32 KiB syscall/IRQ entry reserve. A deep FAT32 syscall therefore overlapped the suspended Ring-3 launch chain. Commit `66fffaf225447261abc264500d5cf6f36165e7b9` fixes the invariant by enlarging the per-thread kernel stack to 96 KiB and reserving 64 KiB for Ring-3 syscall/IRQ entry while retaining a disjoint 32 KiB launch region.

The second closeout failure was a qualification-harness process-budget error, not a kernel regression. With `MAX_PROCESSES=16` / `MAX_THREADS=16`, ten simultaneous one-shot closeout clients overcommitted the bounded table. Commit `76ac39e4421a1ae0ba720f46b040de10cf9e2096` serializes and reaps real probes without raising production limits or weakening required markers.

### Authoritative 3.4 evidence

- Event Broker roundtrip: Actions run `33315953868` — **PASS**.
- Settings persistence: Actions run `33315953774` — **PASS**.
- Notification lifecycle: Actions run `33315953760` — **PASS**.
- Fatal Diagnostic regression after runtime stack fix: Actions run `33315953767` — **PASS**.
- Combined System Services closeout: Actions run `33317140601` — **PASS**, all required roundtrip/liveness/version/filesystem/lookup markers observed.
- System Services regression sweep: Actions run `33317520153` — **PASS**.
  - host kernel/VFS/IPC/TCP/SDK suite PASS;
  - process spawn/wait/reap PASS;
  - kernel and Ring-3 preemption PASS;
  - syscall process ABI PASS;
  - filesystem API PASS;
  - 256× service channel churn PASS;
  - deliberate Ring-3 `clipboardd` crash isolation PASS;
  - clipboard restart + stale connection rebind PASS;
  - clean release media build and OVMF/QEMU runtime PASS.

No known 3.4 release blocker remains after these runs.

---

## ACTIVE DEVELOPMENT

**3.5.0-dev — Connected Userspace**

Status: **IN DEVELOPMENT**

Already present and preserved:
- IPv4, DHCP, DNS, TCP, TLS and HTTPS foundations;
- public DNS A-resolution ABI and Ring-3 integration;
- Event Broker wait/wakeup foundation;
- bounded existing TCP client implementation.

### CURRENT TASK

Implement the real process-owned public socket ABI without recreating the network stack:

1. generation-safe socket identities and bounded socket table;
2. explicit PID ownership and `release_process(pid)` cleanup;
3. public socket type/protocol/status contracts;
4. `socket()` / `close()`;
5. `bind()` / `connect()` / `send()` / `recv()`;
6. real passive TCP foundation before claiming `listen()` / `accept()`;
7. Event-Broker-backed asynchronous UDP/TCP readiness rather than permanent busy-loop polling.

### Road to 15 status

- `3.3.3-dev` — Red Flux — **QUALIFIED**
- `3.4.0-dev` — System Services — **QUALIFIED**
- `3.5.0-dev` — Connected Userspace — **ACTIVE**
- `3.6.0-dev` — Flux Stabilization — pending
- `4.0.0-dev` — Pre-Steel — pending
- `5.0.0-dev` — Steel / Hardware — pending
- `6.0.0-dev` — Core Steel — pending
- `7.0.0-dev` — Iron Shield — pending
- `8.0.0-dev` — Connected Steel — pending
- `9.0.0-dev` — Forge Graphics — pending
- `10.0.0-dev` — Steel Applications — pending
- `11.0.0-dev` — Anvil — pending
- `12.0.0-dev` — Platform / Web — pending
- `13.0.0-dev` — Forge Design — pending
- `14.0.0-rc` — Forge Desktop / RC — pending
- `15.0.0` — STABLE — final target

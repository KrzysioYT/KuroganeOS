# KuroganeOS — Current Release State

Last updated: 2026-09-03

## COMPILED / RUNTIME VERSION

**3.3.3-dev — Red Flux**

The embedded runtime version string intentionally remains `3.3.3-dev`. Road-to-15 engineering milestones are qualified independently and do not silently relabel already-built media.

`3.3.3-dev` remains **QUALIFIED** for its scoped Red Flux UEFI/installer/FAT32/Ring-3/network/TLS/desktop/audio/GOP definition. Oracle VirtualBox remains optional external validation and is not a milestone percentage or release blocker.

---

## QUALIFIED MILESTONES

### 3.4.0-dev — System Services

Status: **QUALIFIED**

Qualified scope includes named IPC, Service SDK/version negotiation, Event Broker, Settings, Notification, Account, Session and Clipboard services, persistent filesystem API, lifecycle cleanup, restart/rebind, bounded service-channel churn and clean OVMF/QEMU combined-runtime regression.

Authoritative closeout evidence:
- combined System Services closeout: Actions run `33317140601` — PASS;
- full 3.4 regression sweep: Actions run `33317520153` — PASS;
- 3.4 regression re-run on the final 3.5 SHA: Actions run `33410600879` — PASS.

### 3.5.0-dev — Connected Userspace

Status: **QUALIFIED**

Connected Userspace is closed from fresh same-SHA runtime evidence at source SHA `7f715a9d654a76b300f1161ba86f4e97fee5e500`.

Qualified scope includes:
- process-owned generation-safe public sockets and exit cleanup;
- UDP roundtrip/readiness and TCP progression/refused/reset/timeout/cleanup;
- DNS Service roundtrip plus crash/restart/rebind;
- live E1000 carrier-driven Network Events through Event Broker to Ring-3;
- verified TLS/HTTPS with CA, hostname, SNI and bounded-response validation;
- asynchronous `audiod.v1`, bounded queues/mixing and AC'97 process-exit cleanup;
- Application Registry manifests/catalog/executable validation/client cleanup;
- full compatibility regression against the already-qualified 3.4 service stack.

Fresh component evidence on the final 3.5 SHA:
- Socket/TCP: run `33410591776` — PASS;
- DNS Service: run `33410593584` — PASS;
- Network Events: run `33410595658` — PASS;
- Audio + App Registry KVM: run `33410597347` — PASS;
- TLS/HTTPS: run `33410598935` — PASS;
- 3.4 regression sweep: run `33410600879` — PASS.

Final Connected Userspace closeout: Actions run `33410583405` — **PASS**. Self-hosted KVM job `99549667506` ran the complete host suite, clean release IMG/ISO build and uninjected production OVMF/q35/KVM runtime with E1000 + Intel ICH AC'97. Required runtime evidence included:

```text
[TEST] dhcp_lease: PASS
[TEST] network_gateway_icmp: PASS
[TEST] ALL_REQUIRED_TESTS_PASSED
[INFO][AC97][CPU0][KERNEL] Intel ICH AC97 PCM output ready (48 kHz S16LE stereo)
[TEST] connected_userspace_closeout: PASS
[closeout] clean OVMF/KVM production regression: PASS
```

No known 3.5 blocker remains.

---

### 3.6.0-dev — Flux Stabilization

Status: **QUALIFIED**

Flux Stabilization is closed from fresh same-SHA evidence at source SHA `0caf8cc42f872b11b44f874029eb41aeae152abc`.

Qualified scope includes bounded retained per-window surfaces, damage-region composition with deterministic fallback, normalized pointer/hover/pressed interaction, process-owned window and surface cleanup, crash isolation, focus/capture repair, and repeated Login → Home → Logout → Login recovery on OVMF/q35/KVM.

Authoritative same-SHA evidence:
- Flux Runtime Core: Actions run `33530401377` — PASS;
- Flux Session Recovery: Actions run `33530403709` — PASS;
- 3.4 System Services regression sweep: Actions run `33530406070` — PASS;
- 3.5 Connected Userspace closeout: Actions run `33530408164` — PASS;
- Flux Stabilization closeout: Actions run `33530392489` — **PASS**.

The closeout host-release job `99931777453` ran the full host suite, rebuilt release IMG/ISO from scratch and required clean production OVMF/KVM boot markers. Same-SHA gate job `99931776975` dispatched and verified all dependent regressions before final closeout job `99934730066` recorded success.

No known 3.6 blocker remains. GPU acceleration remains outside this milestone; Forge Graphics is still a later formal gate.

---

## ACTIVE DEVELOPMENT

**4.0.0-dev — Pre-Steel**

Status: **ACTIVE**

The active KuroFS slice has progressed from allocator and inode-data durability into revision-checked file growth, zero-filled expansion, shrink/truncate reclamation, leak-free directory copy-on-grow, generation-safe copy-on-write unlink and collision-safe same-directory rename on the production block-device contract. Unlink orders parent publication, child tombstoning and extent release to prefer bounded leaks over duplicate ownership after interruption. Cross-directory move and writable VFS integration remain open and are not claimed. Device Model 2.0 work proceeds with capability-scoped ownership and deterministic cleanup; unrestricted PCI/MMIO/I/O-port/DMA access is not exposed to arbitrary Ring-3 applications.

### Road to 15 status

- `3.3.3-dev` — Red Flux — **QUALIFIED**
- `3.4.0-dev` — System Services — **QUALIFIED**
- `3.5.0-dev` — Connected Userspace — **QUALIFIED**
- `3.6.0-dev` — Flux Stabilization — **QUALIFIED**
- `4.0.0-dev` — Pre-Steel — **ACTIVE**
- `5.0.0-dev` — Steel / Hardware — pending
- `6.0.0-dev` — Core Steel — pending
- `7.0.0-dev` — Iron Shield — pending
- `8.0.0-dev` — Connected Steel — pending
- `9.0.0-dev` — Forge Graphics — pending
- `10.0.0-dev` — Steel Applications — pending
- `11.0.0-dev` — Anvil — pending
- `12.0.0-dev` — Platform / Web — pending
- `13.0.0-dev` — Forge Design — pending
- `14.0.0-rc` — Forge Desktop / release candidate — pending
- `15.0.0` — STABLE — final target

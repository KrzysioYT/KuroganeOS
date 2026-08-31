# KuroganeOS — Current Release State

Last updated: 2026-08-31

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

## ACTIVE DEVELOPMENT

**3.6.0-dev — Flux Stabilization**

Status: **IN DEVELOPMENT**

The existing Red Flux Window Core already provides generation-checked window IDs, focus/z-order, drag, interactive resize, minimize/maximize/restore/close, Alt+Tab/Alt+F4, clipping, a software pointer, session ownership and a full-frame software backbuffer.

### CURRENT TASK

Move the desktop from compatibility full-frame presentation toward a bounded native surface/compositor model without rewriting the working Window Core:

1. add bounded per-window surface ownership and generation-safe surface state;
2. add bounded damage regions and clipping with deterministic full-frame fallback;
3. integrate damage with present/move/resize/focus/close paths;
4. guarantee process/window/surface cleanup when an app exits or crashes;
5. then harden focus/input/drag/resize and Login → Home → Login supervision;
6. finish with repeated window/session churn and long-runtime OVMF/KVM qualification.

GPU acceleration is not part of this milestone; Forge Graphics remains a later formal gate.

### Road to 15 status

- `3.3.3-dev` — Red Flux — **QUALIFIED**
- `3.4.0-dev` — System Services — **QUALIFIED**
- `3.5.0-dev` — Connected Userspace — **QUALIFIED**
- `3.6.0-dev` — Flux Stabilization — **ACTIVE**
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
- `14.0.0-rc` — Forge Desktop / release candidate — pending
- `15.0.0` — STABLE — final target

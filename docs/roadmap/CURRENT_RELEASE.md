# KuroganeOS — Current Release State

Last updated: 2026-08-29

## CURRENT FORMAL VERSION

**3.3.3-dev — Red Flux**

Status: **QUALIFIED (SCOPED DEV MILESTONE)**

Progress: **100% of the defined Red Flux scope**

This does not mean that KuroganeOS is feature-complete or stable. Advanced audio services, Intel HDA, hardware GPU acceleration, SMP, NVMe parity, final security isolation, updater/recovery and other later Road-to-15 work are intentionally outside the Red Flux scope and are not counted as missing 3.3.3 work.

The historical branches named `dev/3.3.5-*` through `dev/3.3.9-*` are internal 3.3.3 development workstreams. They are not separate formal releases. There is no formal `3.3.4-dev` release gate.

### Red Flux function status

| Area | Scoped completion | Evidence / boundary |
|---|---:|---|
| Boot / UEFI | 100% | UEFI media, OVMF/QEMU boot qualification |
| Installer | 100% | GPT/ESP/root flow, recoverable profile state, install package/media regression |
| Filesystem | 100% | writable FAT32/VFS and public Ring-3 file API regression |
| Userspace / Ring-3 | 100% | ELF64 processes, syscalls, ownership cleanup, IPC/event/shared-memory regressions |
| Networking | 100% | Red Flux IPv4/DHCP/DNS/TCP scope and E1000/PCnet/VirtIO QEMU NAT matrix |
| TLS / HTTPS | 100% | real guest TLS/HTTPS qualification with certificate validation enabled |
| Desktop | 100% | Red Flux login/session/desktop scope; final Forge Desktop is later work |
| Audio | 100% | Red Flux bounded AC'97 Ring-3 PCM scope; mixer/HDA belongs to later milestones |
| Graphics | 100% | Red Flux GOP/software compositor scope; hardware 3D belongs to Forge Graphics |
| Regression / Closeout | 100% | expanded closeout suite including IPC channel/event/shared-memory |

`100%` above means completion of the deliberately bounded Red Flux scope, not completion of the final OS capability.

### Verified Red Flux evidence

- Userspace handle ownership regression: Actions run `33220748290` — **PASS**.
- Real guest TLS/HTTPS qualification: Actions run `33220761526` — **PASS**.
- Combined UEFI ISO qualification: Actions run `33220774861` — **PASS**.
- Expanded closeout qualification with IPC channel/event/shared-memory in the normal host suite: Actions run `33220980716` — **PASS**.
- Last verified closeout workstream commit: `21ba9a619e6de2ed6bf1510a7676e32313b67138`.

### External validation

Oracle VirtualBox host acceptance is **OPTIONAL / EXTERNAL VALIDATION**. It is not part of version progress, percentage calculation or Definition of Done. Existing VirtualBox tooling remains useful compatibility evidence when an appropriate host is available, but lack of that environment is neither FAIL nor a blocker.

No release tag is created solely from this documentation status.

---

## ACTIVE DEVELOPMENT

**3.4.0-dev — System Services**

Current internal workstream: `dev/3.4.1-event-broker`

Status: **IN DEVELOPMENT**

Estimated scoped progress: **~28%**

The branch suffix `3.4.1` is an internal workstream label, not a separate formal product version. The formal active milestone remains `3.4.0-dev`.

### System Services function status

| Area | Progress | Current truth |
|---|---:|---|
| Service Core | 75% | named service model and lifecycle foundation implemented; metadata/versioning still missing |
| Service Registration | 100% | real named IPC bind path, unique names, PID ownership |
| Service Discovery | 100% | real named connect/discovery path |
| IPC Service Layer | 85% | request/reply transport and connection lifecycle on real named IPC |
| Event Broker | 55% | real Ring-3 service starts; subscribe/publish/unsubscribe implemented; runtime roundtrip currently FAIL |
| Settings Service | 0% | not implemented |
| Notification Service | 0% | not implemented |
| Account Service | 0% | not implemented |
| Session Service | 0% | not implemented as a service architecture component |
| Filesystem API | 70% | clean public Ring-3 FS API already exists; service-layer integration remains to audit/finish |
| Clipboard | 0% | not implemented |
| Reliability / Recovery | 35% | PID cleanup/generation-safe handles exist; service crash/restart recovery missing |
| SDK | 60% | `service.h` and Event Broker protocol exported; examples/docs/helpers incomplete |
| Qualification | 35% | Service Architecture base qualified; Event Broker runtime gate currently failing |

### Completed and verified base

- Real named IPC backend.
- PID-owned endpoints/connections.
- Generation-safe IPC/event/shared-memory handles.
- Process-exit cleanup foundation.
- Public `kurogane/service.h` over the real IPC backend.
- Service Architecture full qualification: Actions run `33221125505` — **PASS**.
- Public Event Broker protocol and ABI size/constant assertions implemented at current HEAD.

### CURRENT TASK

**Event Broker runtime qualification**

Implemented code currently provides:
- real `events.v1` Ring-3 service endpoint;
- bounded client and subscription tables;
- per-PID subscriptions;
- subscribe / publish / unsubscribe requests;
- real kernel event creation, grant and signal delivery;
- cleanup when a client connection closes;
- public Event Broker protocol SDK;
- a Ring-3 qualification probe.

Current result:
- media/build step on Actions run `33221674569`: **PASS**;
- `eventd` service startup marker: **PASS**;
- actual subscribe → publish → wait → unsubscribe roundtrip: **FAIL**.

Therefore Event Broker is **IMPLEMENTED / PARTIALLY TESTED / NOT QUALIFIED**.

### FAIL

- Event Broker runtime roundtrip: Actions run `33221674569` — **FAIL** at the required runtime marker. This is an active implementation defect, not an environmental blocker.

### PENDING / UNVERIFIED

- Event Broker negative cases and stress coverage.
- Event Broker full regression after runtime fix.
- Cross-host userspace builder parity for the new service binaries.
- Service metadata and version negotiation.
- Settings, notifications, account, session, clipboard and service recovery workstreams.

### LAST COMMIT

Current development HEAD before this documentation update: `6e4ad9975a65fa4b36e761cedce1012566b8e4b3`

`test: lock event broker protocol ABI`

### NEXT ACTION

Diagnose the real Event Broker roundtrip failure, add stage-specific negative diagnostics, fix the backend/protocol defect, rerun the real QEMU Ring-3 roundtrip, then run broader regression. After a real PASS, continue automatically with service metadata/version negotiation.

---

## ROAD TO 15 — FORMAL MILESTONES

- `3.3.3-dev` — Red Flux — **100% scoped / QUALIFIED**
- `3.4.0-dev` — System Services — **~28% / IN DEVELOPMENT**
- `3.5.0-dev` — Connected Userspace — **0%**
- `3.6.0-dev` — Flux Stabilization — **0%**
- `4.0.0-dev` — Pre-Steel — **0%**
- `5.0.0-dev` — Steel / Hardware — **0%**
- `6.0.0-dev` — Core Steel — **0%**
- `7.0.0-dev` — Iron Shield — **0%**
- `8.0.0-dev` — Connected Steel — **0%**
- `9.0.0-dev` — Forge Graphics — **0%**
- `10.0.0-dev` — Steel Applications — **0%**
- `11.0.0-dev` — Anvil — **0%**
- `12.0.0-dev` — Platform / Web — **0%**
- `13.0.0-dev` — Forge Design — **0%**
- `14.0.0-rc` — Forge Desktop — **0%**
- `15.0.0` — STABLE — **0%**

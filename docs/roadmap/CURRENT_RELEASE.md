# KuroganeOS — Current Release State

Last updated: 2026-09-23

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

### 4.0.0-dev — Pre-Steel

Status: **QUALIFIED**

Pre-Steel is closed at source SHA `bbec12248773930d6f40aa98837ff13e99b7cf5e`. Qualified scope includes KuroFS v1 metadata and writable VFS integration, native two-boot persistence, Device Model 2.0 generation-safe handles and capability boundaries, read-only Ring-3 device discovery, Driver Manager 2.0 failure isolation, unified status semantics and the qualified Fatal Diagnostic path.

Authoritative same-SHA evidence:
- Pre-Steel closeout run `34260827773` — **PASS**;
- final authoritative job `102186252372` — **PASS**;
- final gate: `KuroganeOS 4.0 Pre-Steel closeout: PASS sha=bbec12248773930d6f40aa98837ff13e99b7cf5e`.

---

## ACTIVE DEVELOPMENT

**5.0.0-dev — Steel / Hardware**

Status: **ACTIVE**

Bounded single-vector MSI is qualified at `718d8c546b4eb436be382f847f910a62b3714220` by Actions run `34292320432`, job `102281331383`. Bounded single-vector MSI-X is qualified at `745793376abf6cb5f88a4410236b4b2ad6a2c1ca` by run `34358861039`, job `102490389818`: real QEMU Intel 82574L delivery through the mapped MSI-X table, generation-owned vector, Local APIC and production IDT, followed by ordered teardown. Pre-Steel regression closeout also passed on that source in run `34358861380`. Multi-vector routing, production-driver adoption, SMP and the remaining hardware matrix stay open; legacy PIC/PIT remains the fallback during migration.

### Road to 15 status

Current verified source `308f2fb75205dfa265c8a620cf88e84d0627742d` passed the VirtIO Steel qualification matrix. Network Cleanup run `34950706350` / job `104320726734` and MSI-X Runtime run `34950706505` / jobs `104320726926`, `104320727235` passed. The matrix covers malformed used-ring completion rejection without partial ownership mutation, legal 16-bit completion wrap, active-queue reset/cleanup, real queue IRQ delivery, two-vector RX/TX delivery, and truthful no-MSI-X polling fallback. Clean release-media builds passed in both workflows.

Bounded MSI-X groups are therefore qualified for the production VirtIO transport scope. Full 5.0 qualification is not claimed: SMP, PCIe/ACPI/APIC/HPET hardening, NVMe, USB/xHCI, Intel HDA and real-hardware coverage remain open.

Current regression evidence at `bcbcd9b18a7b4a07d4d1022930acc29eb77e8075`:
all eleven triggered workflows passed, including VirtIO active-queue reset/
cleanup/retry (`34546210440`), Pre-Steel closeout (`34546210640`), System
Services regression (`34546210434`) and Fatal Diagnostic (`34546210350`).
VirtIO-net now has production shared RX/TX MSI-X notification and typed polling
fallback, with host-tested cleanup and a dedicated delivery/teardown runtime
gate. Shared-vector delivery and cleanup are **QUALIFIED** at
`6898c72f273207dbfd526f78a640433932e2291c` by QEMU run `34683815908`, job
`103527144398`. This source also passed 3.4 regression `34683815923`, real
TLS/HTTPS `34683815909` and PCI MSI/MSI-X transport `34683815912`/`34683815880`.
Production boot with MSI-X absent passed at `cb352b7` in `34684091876` and
again at `d42d9e3` in `34811940595`. Full 5.0 qualification and SMP remain open.

The current Steel engineering slice implements bounded MSI-X route groups and
a two-vector e1000e runtime gate. Host ownership/rollback tests and three local
two-vector QEMU boots passed, alongside a single-vector regression;
exact-source Actions run `34811940628` subsequently qualified both modes. See
[PCI_MSIX_GROUPS.md](../PCI_MSIX_GROUPS.md) for the API contract and evidence.

The APIC routing foundation now also resolves ISA legacy IRQs through ACPI MADT overrides before GSI programming. Invalid reserved flags and conflicting overrides are rejected deterministically; the host contract passed as `[test_io_apic_irq] PASS` in current-head run `34953615582`, job `104330260616`. This remains a routing foundation: firmware I/O APIC delivery, SMP interrupt routing and the broader hardware matrix are still open. Redirection span validation now rejects zero-length, overflowing and overlapping controller GSI ranges before they are accepted.

### USB keyboard and cleanup evidence

The bounded xHCI keyboard path now has real OVMF/QEMU evidence at
`648b9d38cabf8fcabb66ab3e5071f676f18ad98a`: run
[`35085130775`](https://github.com/KrzysioYT/KuroganeOS/actions/runs/35085130775),
job `104758052878`, passed the full host suite, clean release-media build,
enumeration, two ordered hardware HID press/release pairs accepted by the
input queue, and an empty-controller boot requiring acknowledged halt and
DMA/MMIO cleanup. Host fault injection additionally covers timeout quarantine,
failed release retry and stale device handles. It does not establish hotplug,
multiple keyboards, hubs, mouse, mass storage or physical hardware support.

Same-source regressions passed: System Services closeout `35085130746`,
3.4 sweep `35085130828`, Settings persistence `35085130736`, Notifications
`35085130766`, Socket Core `35085130731`, DNS `35085130759`, Fatal Diagnostic
`35085130812`, MSI `35085130779`, MSI-X `35085130744`, VirtIO cleanup
`35085130849` and VirtIO MSI-X `35085130851`. Pre-Steel closeout `35085131514`
and some self-hosted component jobs are still queued; historical qualification
is preserved but a complete current-source matrix is not yet claimed.

The extended USB gate is now **PASS** at production source
`d96c9c8b631c1c60799aed1a9b80f85e0fb81865`, run
[`35086010272`](https://github.com/KrzysioYT/KuroganeOS/actions/runs/35086010272),
job `104760879337`: full host suite, clean release IMG/ISO/install.pkg,
130 real F12 press/release pairs, transfer/event ring cycle wrap and empty
controller halt/cleanup. This source also rejects malformed and duplicate
completion pointers before report DMA reuse; the host regression reproduced
the previous premature retirement and now passes, including ASan/UBSan.

Latest same-source regressions passed: 3.4 sweep `35086010366`, Socket Core
`35086010286`, DNS `35086010335`, Fatal Diagnostic `35086010379`, MSI
`35086010325`, MSI-X `35086010292`, VirtIO cleanup `35086010369`.
Pre-Steel closeout `35086010691`, KuroFS `35086010340`, Device/Driver
`35086010345` and Unified Status `35086010311` remain queued at this update.
No completed current-source job failed, but this is not a complete fresh
Pre-Steel matrix. The earlier ring gate `35085576426` failed before QEMU due
to a timeout argument outside the harness contract; it was corrected to the
existing 240-second limit without reducing the 130-pair test.

USB disconnect/reconnect is now qualified at exact source
`4b33bfbd05bc8b3f01c1cf89bc44ceabb33da79c`, run `35231888211`, job
`105237832241`: three real QMP removal/addition cycles with held-Shift release,
stale device-handle rejection and F12 input after re-enumeration. All twenty
triggered workflows passed on this source, including Pre-Steel closeout
`35231888631`; the queued older regression snapshots above are historical.

First keyboard attachment after booting an empty xHCI controller is qualified
at `ad6a19b11d83d0eecaba3599c084321768371e67`, run `35905197560`:
runtime job `107331023235` and cleanup job `107331023071`. The gate requires
empty-controller readiness, Login before first QMP attachment, then three
held-Shift disconnect/reconnect cycles with successful F12 input. Earlier
130-pair/ring-wrap coverage also passed. Local full host suite, clean release
IMG/ISO/install.pkg and uninjected OVMF production Login + DHCP/gateway passed.
Nine triggered workflows passed at this snapshot; six self-hosted workflows,
including Pre-Steel `35905199805`, are queued, not a fresh full-matrix PASS.

Next exact USB task: HID mouse, starting with all-or-nothing publication of a
mouse report into the common input queue. Retaining a report on queue pressure
must not double-apply pointer movement or publish only part of a button change.
**5.0 remains ACTIVE**, not qualified.

- `3.3.3-dev` — Red Flux — **QUALIFIED**
- `3.4.0-dev` — System Services — **QUALIFIED**
- `3.5.0-dev` — Connected Userspace — **QUALIFIED**
- `3.6.0-dev` — Flux Stabilization — **QUALIFIED**
- `4.0.0-dev` — Pre-Steel — **QUALIFIED**
- `5.0.0-dev` — Steel / Hardware — **ACTIVE**
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

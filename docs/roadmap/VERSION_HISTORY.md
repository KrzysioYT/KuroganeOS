# KuroganeOS Version History

This file tracks formal product milestones. Branch/task suffixes are preserved only as historical engineering references.

## Versioning rule

Formal Road-to-15 milestones are:

`3.3.3-dev` → `3.4.0-dev` → `3.5.0-dev` → `3.6.0-dev` → `4.0.0-dev` → `5.0.0-dev` → `6.0.0-dev` → `7.0.0-dev` → `8.0.0-dev` → `9.0.0-dev` → `10.0.0-dev` → `11.0.0-dev` → `12.0.0-dev` → `13.0.0-dev` → `14.0.0-rc` → `15.0.0`.

Historical names `3.3.4` through `3.3.9`, and internal names such as `3.4.1`, are workstream/branch labels rather than separate formal product releases.

## 3.3.3-dev — Red Flux

Status: **QUALIFIED scoped DEV milestone**.

Initial baseline: `17bd55091c63544b9585840192f0eb288e9cffff`.

Red Flux was completed through internal installer, networking, TLS/HTTPS, userspace ownership, desktop/audio and regression workstreams. The reopened Kurogane Fatal Diagnostic release gate is also qualified. Oracle VirtualBox host execution remains optional external compatibility validation.

Representative closeout evidence:
- `33220748290` — userspace ownership regression PASS;
- `33220761526` — real guest TLS/HTTPS PASS;
- `33220774861` — combined UEFI/media/network qualification PASS;
- `33220980716` — expanded IPC closeout PASS;
- `33315953767` — Fatal Diagnostic regression PASS after later runtime-stack hardening.

## Historical Red Flux workstream labels

- `dev/3.3.5-installer-reliability` — internal installer reliability workstream.
- `dev/3.3.6-network-stabilization` — internal network stabilization workstream.
- `dev/3.3.7-tls-foundation` — internal TLS foundation workstream.
- `dev/3.3.8-userspace-io` — internal userspace I/O workstream.
- `dev/3.3.9-red-flux-closeout` — internal Red Flux closeout workstream.

These are not entries in the formal release sequence.

## 3.4.0-dev — System Services

Status: **QUALIFIED**.

Qualified scope includes named IPC, generation-safe service/event handles, explicit PID ownership and cleanup, public Service SDK/version negotiation, Event Broker, Settings, Notification, Account, Session and Clipboard services, public persistent filesystem access, restart/rebind recovery and bounded service-channel churn.

Authoritative evidence:
- Event Broker `33315953868` — PASS;
- Settings persistence `33315953774` — PASS;
- Notification lifecycle `33315953760` — PASS;
- combined System Services closeout `33317140601` — PASS;
- full 3.4 regression sweep `33317520153` — PASS;
- full 3.4 regression repeated on final 3.5 SHA: `33410600879` — PASS.

## 3.5.0-dev — Connected Userspace

Status: **QUALIFIED**.

Final qualification source SHA: `7f715a9d654a76b300f1161ba86f4e97fee5e500`.

Qualified scope includes process-owned public sockets and cleanup, UDP readiness, TCP lifecycle/error/timeout handling, DNS Service and restart/rebind, live Network Events, verified TLS/HTTPS, asynchronous Audio Service, Application Registry and cross-regression with System Services.

Fresh same-SHA evidence:
- Socket/TCP `33410591776` — PASS;
- DNS Service `33410593584` — PASS;
- Network Events `33410595658` — PASS;
- Audio + Application Registry KVM `33410597347` — PASS;
- TLS/HTTPS `33410598935` — PASS;
- 3.4 regression sweep `33410600879` — PASS;
- Connected Userspace closeout `33410583405` — PASS;
- final self-hosted KVM job `99549667506` — PASS with clean media and uninjected production OVMF boot.

Final runtime evidence included DHCP, gateway ICMP, global required tests, real Intel ICH AC'97 initialization and `[TEST] connected_userspace_closeout: PASS`.

## 3.6.0-dev — Flux Stabilization

Status: **ACTIVE**.

Current work begins with bounded native per-window surfaces and damage regions while preserving the existing Red Flux Window Core and compatibility `KU_SYS_UI_PRESENT` path. Follow-up gates cover focus/input/drag/resize ownership, GUI crash isolation, Login → Home → Login recovery and long-runtime window/session churn.

## Future formal milestones

- `4.0.0-dev` — Pre-Steel.
- `5.0.0-dev` — Steel / Hardware.
- `6.0.0-dev` — Core Steel.
- `7.0.0-dev` — Iron Shield.
- `8.0.0-dev` — Connected Steel.
- `9.0.0-dev` — Forge Graphics.
- `10.0.0-dev` — Steel Applications.
- `11.0.0-dev` — Anvil.
- `12.0.0-dev` — Platform / Web.
- `13.0.0-dev` — Forge Design.
- `14.0.0-rc` — Forge Desktop.
- `15.0.0` — first STABLE release.

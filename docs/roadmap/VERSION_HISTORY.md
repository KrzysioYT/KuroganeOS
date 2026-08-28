# KuroganeOS Version History

This file tracks formal product milestones. Branch/task suffixes are preserved only as historical engineering references.

## Versioning rule

Formal Road-to-15 milestones are:

`3.3.3-dev` → `3.4.0-dev` → `3.5.0-dev` → `3.6.0-dev` → `4.0.0-dev` → `5.0.0-dev` → `6.0.0-dev` → `7.0.0-dev` → `8.0.0-dev` → `9.0.0-dev` → `10.0.0-dev` → `11.0.0-dev` → `12.0.0-dev` → `13.0.0-dev` → `14.0.0-rc` → `15.0.0`.

Historical names `3.3.4` through `3.3.9`, and internal names such as `3.4.1`, are workstream/branch labels rather than separate formal product releases.

## 3.3.3-dev — Red Flux

Status: **QUALIFIED scoped DEV milestone**.

Initial baseline: `17bd55091c63544b9585840192f0eb288e9cffff`.

The Red Flux milestone was subsequently completed through internal workstreams covering installer reliability, network stabilization, TLS/HTTPS, userspace I/O/resource ownership and regression closeout. Those changes remain logically part of 3.3.3-dev rather than creating additional formal 3.3.x releases.

Verified closeout evidence includes Actions runs:
- `33220748290` — userspace ownership regression PASS;
- `33220761526` — real guest TLS/HTTPS PASS;
- `33220774861` — combined UEFI/media/network qualification PASS;
- `33220980716` — expanded closeout including IPC channel/event/shared-memory PASS.

Last closeout workstream commit recorded: `21ba9a619e6de2ed6bf1510a7676e32313b67138`.

Oracle VirtualBox host execution is optional external compatibility validation and is not a formal-version blocker.

## Historical Red Flux workstream labels

- `dev/3.3.5-installer-reliability` — internal installer reliability workstream.
- `dev/3.3.6-network-stabilization` — internal network stabilization workstream.
- `dev/3.3.7-tls-foundation` — internal TLS foundation workstream.
- `dev/3.3.8-userspace-io` — internal userspace I/O workstream.
- `dev/3.3.9-red-flux-closeout` — internal Red Flux closeout workstream.

These are not entries in the formal release sequence.

## 3.4.0-dev — System Services

Status: **IN DEVELOPMENT**.

Qualified foundation:
- named IPC registration/discovery;
- PID ownership and process-exit cleanup;
- generation-safe IPC/event/shared-memory handles;
- public Service SDK;
- Service Architecture qualification run `33221125505` PASS.

Current internal workstream: Event Broker (`dev/3.4.1-event-broker`). A real `events.v1` Ring-3 endpoint, subscription table, event grants/signals and public protocol exist. Runtime roundtrip run `33221674569` currently FAILS, so Event Broker is not yet qualified.

## Future formal milestones

- `3.5.0-dev` — Connected Userspace.
- `3.6.0-dev` — Flux Stabilization.
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

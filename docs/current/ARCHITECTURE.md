# KuroganeOS current architecture

## Layering

KuroganeOS keeps responsibilities separated as:

`UEFI boot -> kernel/core -> drivers -> kernel primitives -> system services -> userspace libraries/SDK -> desktop/applications`

Application/service policy should remain outside the kernel unless a kernel primitive is required for protection, scheduling, memory, device, filesystem or IPC semantics.

## Kernel and Ring-3 foundation

The current source tree provides native x86-64 UEFI boot, virtual memory, scheduler/process/thread foundations, ELF64 Ring-3 execution, syscall ABI, VFS/FAT32, networking, TLS, audio/graphics foundations and kernel IPC primitives.

Process-owned resources use generation-safe handles where implemented. Process exit performs cleanup for IPC, shared memory and event access so a dead PID does not intentionally retain those kernel objects.

## Named IPC and services

The System Services milestone builds on the existing named IPC backend rather than introducing a fake or duplicate registry.

The current public Service SDK maps directly to real named IPC operations:
- register/bind a unique service name;
- connect/discover a named service;
- accept a connection;
- send and receive bounded messages;
- close endpoint/connection handles.

Endpoint and connection ownership is tied to real PIDs. Handles are generation-safe and peer/process cleanup is performed by the IPC backend.

## Event primitives

Kernel event objects are real waitable IPC primitives with generation-safe handles and per-PID access grants. They support create, grant, signal, reset, poll/wait and close. The Event Broker uses these primitives for delivery instead of simulating notifications in userspace.

## Event Broker workstream

The active Ring-3 service is `events.v1`.

Implemented design:
- bounded client table;
- bounded subscription table;
- topic validation;
- subscriber PID ownership;
- SUBSCRIBE creates a real auto-reset event and grants access to the subscribing PID;
- PUBLISH signals matching real event objects;
- UNSUBSCRIBE closes the broker-owned event and clears the subscription;
- client disconnect cleanup removes subscriptions owned by that client PID.

Qualification state at this document revision: **IMPLEMENTED / PARTIALLY TESTED / NOT QUALIFIED**. The service registers and runs in QEMU, but the real subscribe request currently times out waiting for the server reply. Server-side accept/receive/reply diagnostics are active; no PASS is claimed until the full Ring-3 roundtrip succeeds.

## Formal milestone boundaries

`3.3.3-dev — Red Flux` is qualified for its bounded scope. Hardware 3D, HDA/mixer, SMP, NVMe, final security isolation and updater/recovery belong to later formal milestones and are not retroactively counted as Red Flux defects.

`3.4.0-dev — System Services` owns service architecture, Event Broker, settings, notifications, accounts, sessions, filesystem service APIs, clipboard and service recovery/reliability.

## External validation

Oracle VirtualBox host execution is optional external compatibility validation. It remains useful when available, but it is excluded from formal progress and Definition of Done. QEMU/OVMF and completed automated runtime gates are the currently executable virtual-platform evidence.

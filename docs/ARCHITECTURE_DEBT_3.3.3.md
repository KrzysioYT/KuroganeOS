# KuroganeOS 3.3.3-dev architecture debt

Status: **OPEN / NOT COMPLETE — release-blocking MUST HAVE work remains.**

This is a concrete debt list, not a roadmap of features. The goal is to stop
kernel mechanisms, Red Flux policy, recovery code and test scaffolding from
becoming mutually dependent as the system grows.

**3.3.3-dev must not be considered architecture-complete while any item marked
`MUST HAVE` below is missing or only represented by a mock/stub.**

Linux may be inspected for architecture and public-interface concepts only.
No Linux implementation code is to be copied into KuroganeOS.

## 1. Remove dead GUI control from the emergency kernel shell

`kernel/shell/shell.cpp` still contains `apps`, `run` and `gui` branches plus
an `experimental_gui_enabled` switch and a dependency on the kernel
`applications` framework.

The normal installed boot does not use that shell: `kmain()` starts the kernel
shell only in Safe Mode and passes `false`. Therefore the GUI branches are dead
for the supported Safe Mode path and only increase coupling and maintenance
cost.

Target:

- kernel shell is an emergency/recovery console only;
- no Red Flux application listing or launching;
- no dependency on `kernel/apps/framework.hpp`;
- recovery commands may inspect kernel mechanisms, but desktop policy remains
  userspace-owned.

## 2. Eliminate dual desktop/session ownership

Current desktop boot has two owners:

- `/system/init` is spawned as PID 1 and supervises the Red Flux login/session;
- kernel `main.cpp` also initializes the legacy application framework and later
  attempts `applications::launch("desktop")`.

This explains the recurring diagnostic `desktop auto-launch failed: another
application is running`: the kernel compatibility path is competing with the
Ring-3 session model.

Target:

- PID 1 owns login/session startup and recovery;
- the kernel does not launch a named `desktop` application;
- once PID 1 is proven, remove the legacy desktop autolaunch compatibility
  path instead of adding more state checks around it.

## 3. Move Red Flux product policy out of the kernel window manager

`kernel/ui/window_manager.cpp` currently embeds product-facing dock entries,
application titles, shortcut keys, login/home special cases and desktop layout
policy.

Target split:

- kernel: buffer/scanout, generic window/surface handles while compatibility is
  needed, input delivery, IPC/resource validation;
- Ring-3 Red Flux service: dock catalogue, focus policy, app launch policy,
  login/home semantics, pinned apps and presentation.

Do not attempt a one-commit compositor rewrite. First stabilize an IPC/UAPI
boundary, then move policy behind it.

## 4. Stop hard-coding NIC implementation names in generic boot tests

`main.cpp` currently prints `E1000 link READY` and emits `e1000_link` whenever
`net::service::physical_interface()` exists. That text is wrong when a PCnet or
VirtIO backend is active and makes VM debugging misleading.

Target:

- query the active physical backend or expose a stable interface/driver name;
- generic tests should say `physical_link` unless they are explicitly testing
  one driver implementation;
- adapter-specific qualification belongs in driver-specific tests.

## 5. Serialize diagnostic output

Kernel logging builds one logical line from multiple terminal/serial writes.
Userspace tests can write at the same time, so serial output can interleave,
for example a test marker can be inserted inside a TLS error line.

Target:

- one logical log record is emitted atomically;
- IRQ-safe design must avoid self-deadlock if an interrupt logs while another
  context owns the output path;
- smoke tests must not depend on parsing text that can be torn by concurrent
  writers.

## 6. Finish terminal/shell separation

The graphical terminal and recovery console now use the desktop-neutral
`ku_shell_*` frontend boundary. The underlying `shell_core.h` still keeps
legacy `flux_shell_*` implementation identifiers and is header-only.

Target:

- rename/remove the legacy implementation identifiers after the frontend
  migration is proven;
- compile the command core once as a userspace library/object rather than a
  private static-inline copy in every frontend;
- keep builtins only for shell-local state such as `cd`, `exit`, history and
  job control;
- resolve ordinary commands as Ring-3 executables through a search path.

## 7. Treat stale build/media mismatches as a first-class failure

A stale ISO can legitimately boot and produce old test markers while a newer
PowerShell smoke helper interprets them using a newer contract, or vice versa.
Version `3.3.3-dev` alone is insufficient to prove source/media coherence.

Target:

- embed a source/build identifier in the loader/kernel and install media;
- smoke helper prints and verifies the identifier before qualification;
- reject obsolete markers such as the pre-policy terminal smoke marker with an
  explicit `stale media/helper mismatch` error instead of waiting for a generic
  timeout.

## 8. Persistent-root tests must report the failed filesystem operation

The current installer already creates the canonical root layout including
`/var` and `/var/log`. A `fat32_persistence: FAIL` from media built before that
layout fix is expected to be stale-media behavior, not evidence that `/var`
needs another package placeholder.

If the failure reproduces on freshly rebuilt media, instrument the persistence
probe to report the exact failing operation and status: create, open, write,
close, sync, reopen, readback or comparison.

## 9. TLS/TCP failures need typed diagnostics before timeout tuning

The current TLS BIO collapses a TCP `InterfaceError` into a generic Mbed TLS
internal error. The observed trust-store parse now succeeds, so a later BIO send
failure must be diagnosed at the transport boundary instead of being presented
as a generic certificate/RTC/trust error.

Target:

- distinguish TCP RST, peer close, invalid state, ACK timeout and retry
  exhaustion;
- log connection state and sequence progress on terminal failure without
  leaking payload data;
- propagate a typed transport reason to TLS and then to Kurogane Web;
- only tune retry/timeout values after the failing transport condition is known.

## 10. MUST HAVE — Kurogane Fatal Diagnostic Screen / kernel panic snapshot

**Release gate:** 3.3.3-dev is not considered complete until KuroganeOS has a
real fatal-error diagnostic path. This must be implemented from live kernel
state; a static mock screen, PNG-only UI or hard-coded example values do not
satisfy this requirement.

The purpose is not to imitate Windows BSOD. KuroganeOS must expose its own
Red Flux-compatible fatal diagnostic surface: near-black/graphite background,
minimal red accents, monospaced text and primitives rendered directly by the
system wherever possible. External bitmap assets are optional and must never be
required for the diagnostic information itself.

### Required panic snapshot

At the first fatal transition, before normal scheduling/recovery can destroy
useful context, capture an immutable best-effort snapshot containing at least:

- panic/exception code and symbolic reason;
- faulting module/component and source/build identifier when available;
- CPU/APIC id, current PID/TID and process/thread name;
- RIP, RSP, RFLAGS and general-purpose x86-64 registers;
- fault address and architecture-specific exception data such as CR2/error code
  for page faults;
- privilege level / execution context (kernel, userspace, IRQ where known);
- bounded stack trace / return-address trace with symbol names when available;
- kernel version, full `3.3.3-dev` build id and commit/source identifier;
- system uptime and wall-clock timestamp when RTC time is trustworthy;
- physical/virtual memory usage and paging state;
- active driver/device context when the failure can be attributed safely;
- bounded `LAST KERNEL EVENTS` ring containing the final diagnostic records
  immediately preceding the panic;
- dump status, path/id and whether the dump is full, partial or unavailable.

### LAST KERNEL EVENTS

Maintain a bounded, allocation-safe kernel diagnostic ring so the panic screen
can show the final sequence that led to the failure instead of only the final
exception. The panic path must be able to read this ring without allocating
memory or depending on userspace.

Each event should carry, where available:

- monotonic timestamp;
- subsystem/category;
- severity;
- CPU id;
- PID/TID;
- short event code;
- bounded text payload with secrets and user payload data excluded.

A typical diagnostic chain should make sequences such as
`driver init -> allocation -> IRQ -> page fault -> panic` visible to a developer.

### Panic renderer requirements

- no dependency on Red Flux userspace being alive;
- no dependency on heap allocation after the panic transition where avoidable;
- render through an emergency framebuffer/text path using fonts/primitives
  already resident in memory;
- remain legible if only a minimal text renderer is available;
- deterministic layout at supported framebuffer sizes with graceful truncation;
- serial output must mirror the essential panic data for headless debugging;
- nested/double panic falls back to a smaller guaranteed-safe emergency view;
- the diagnostic screen must never attempt ordinary window management,
  application launching or network access.

### Crash dump

Provide a bounded crash-dump writer when the storage path is known safe.
A dump failure must never replace or hide the original panic.

Minimum metadata:

- dump format/version;
- kernel/build/commit id;
- panic reason and registers;
- process/thread identity;
- stack trace;
- last kernel events;
- selected memory/system metadata;
- checksum/integrity field when practical.

If persistent storage cannot be trusted, keep the screen + serial snapshot and
report `DUMP UNAVAILABLE` with a typed reason rather than blocking indefinitely.

### Recovery behavior

The screen may expose a restart action only after the diagnostic snapshot is
stable. Automatic restart must be optional/development-policy controlled so a
developer can inspect the panic indefinitely.

The footer should expose concise state such as:

- `STATUS: CRITICAL`;
- dump state;
- safe-mode availability;
- restart key when supported.

### Acceptance criteria

This MUST HAVE is complete only when all of the following are proven:

1. A deliberately triggered kernel exception reaches the fatal screen.
2. Displayed registers and fault metadata come from the actual trap frame.
3. PID/TID/process information matches the faulting execution context.
4. `LAST KERNEL EVENTS` contains real events emitted before the test panic.
5. Essential diagnostics are mirrored to serial output.
6. A dump is written and can be parsed, or a typed safe failure reason is shown.
7. A second panic inside the normal panic path reaches the minimal fallback
   instead of recursively crashing forever.
8. QEMU and VirtualBox smoke tests can detect a deliberate panic marker without
   confusing it with an ordinary boot failure.
9. No PNG or external visual asset is necessary to understand or operate the
   fatal diagnostic view.
10. No mock values, TODO-only paths or hard-coded register/stack examples remain
    in the production panic implementation.

## Recommended migration order

1. Rebuild current media and prove the stale-artifact mismatch is gone.
2. Remove dead GUI commands from the Safe Mode kernel shell.
3. Remove kernel desktop autolaunch after PID 1 handoff.
4. Make NIC/test labels backend-neutral and serialize diagnostics.
5. Instrument the remaining persistence and TLS/TCP failures.
6. Build the allocation-safe diagnostic ring and trap-frame panic snapshot.
7. Implement the Kurogane Fatal Diagnostic Screen, serial mirror and nested-panic fallback.
8. Add bounded crash-dump persistence and deliberate panic smoke coverage.
9. Convert userspace shell command dispatch toward executable lookup.
10. Establish generic display/input IPC and migrate Red Flux dock/window policy
    out of the kernel incrementally.

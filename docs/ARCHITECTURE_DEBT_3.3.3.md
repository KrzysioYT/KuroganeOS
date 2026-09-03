# KuroganeOS 3.3.3-dev architecture debt

Status: post-VirtualBox audit, clean-room architecture work.

This is a concrete debt list, not a roadmap of features. The goal is to stop
kernel mechanisms, Red Flux policy, recovery code and test scaffolding from
becoming mutually dependent as the system grows.

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

The persistence probe now reports the exact failing operation and VFS status:
create, open, write, close, sync, readback or comparison (including the
comparison offset). A fresh-media runtime run is still required to determine
whether the remaining failure is in the FAT32/AHCI path or in stale media.

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

## Recommended migration order

1. Rebuild current media and prove the stale-artifact mismatch is gone.
2. Remove dead GUI commands from the Safe Mode kernel shell.
3. Remove kernel desktop autolaunch after PID 1 handoff.
4. Make NIC/test labels backend-neutral and serialize diagnostics.
5. Instrument the remaining persistence and TLS/TCP failures.
6. Convert userspace shell command dispatch toward executable lookup.
7. Establish generic display/input IPC and migrate Red Flux dock/window policy
   out of the kernel incrementally.

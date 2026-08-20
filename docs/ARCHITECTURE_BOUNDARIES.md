# KuroganeOS architecture boundaries

Status: design rule for 3.3.x and later.

This document defines boundaries intended to keep KuroganeOS maintainable as the system grows. The design is clean-room: Linux and other operating systems may be studied for architectural concepts and public interfaces, but implementation code must not be copied into KuroganeOS.

## Core rule: mechanism in kernel, policy in userspace

The kernel owns mechanisms that require privilege or hardware access. Product behavior, desktop policy and application-specific decisions belong in Ring 3.

### Kernel responsibilities

- CPU, interrupts, timers and scheduler primitives.
- Process/address-space creation, execution, waiting and teardown.
- Memory management and syscall validation.
- Block, network, input and display device drivers.
- VFS/filesystem mechanisms and permissions.
- Generic IPC, event queues and shared-memory primitives.
- Generic input event delivery.
- Generic display/scanout/buffer primitives.
- Generic byte-stream/terminal transport primitives.

The kernel must not know names such as `RED FLUX HOME`, `KUROGANE WEB`, `FILES`, `PERFORMANCE`, launcher shortcuts or terminal commands except for temporary compatibility code with a tracked removal plan.

### Userspace responsibilities

- PID 1 service/session supervision.
- Red Flux session policy.
- Login policy and account UX.
- Window placement, dock/launcher policy and application catalogue.
- Terminal emulator presentation.
- Shell parsing, builtins, command lookup, history and job-control policy.
- Browser/UI policy.
- User-facing network tools such as `ping`, `ip`, `nslookup` and similar utilities.

## Terminal and shell boundary

The graphical terminal and recovery console are frontends. They must not contain shell policy and they must not know how Red Flux launches or manages desktop surfaces.

A terminal frontend provides input/output transport. A shell consumes that transport and performs command parsing/execution. GUI-specific rendering belongs only to the graphical frontend.

The public Kurogane terminal frontend API uses `ku_shell_*` names. Legacy `flux_shell_*` identifiers inside the current command-core implementation are an internal migration detail and must not leak back into frontends.

Longer term the command core should move from the current header-only implementation to a normal userspace library/object so console and GUI terminal do not compile private copies of the parser.

## Command execution model

Avoid a growing central `if/else` table for every system feature.

Builtins should be limited to commands that must modify shell-local state, for example `cd`, `exit`, `history`, `jobs`, `wait` and basic execution control. Normal tools should be executable Ring-3 programs resolved through a search path.

Examples:

- `ping` -> userspace executable using network syscalls/capabilities.
- `ls` -> userspace executable using filesystem syscalls.
- `tasks` -> userspace executable using process-query syscalls.
- `diskinfo` -> userspace executable using device/storage query syscalls.

The shell must not contain GUI launcher aliases or privileged kernel-console backdoors.

## Graphics / Red Flux boundary

The current 3.3.x window manager still contains significant Red Flux policy in the kernel. Treat this as a migration state, not the final architecture.

Target model:

1. Kernel exposes framebuffer/scanout, buffer, input-event and IPC primitives.
2. A Ring-3 Red Flux compositor/session service owns desktop policy.
3. GUI applications communicate with that service through stable Kurogane UI IPC/syscalls.
4. The compositor decides window placement, focus, dock entries, shortcuts and presentation.
5. Kernel code validates resources and performs privileged operations; it does not decide which product application belongs on the dock.

Do not move all of this at once. First establish stable user/kernel interfaces, then migrate policy behind those interfaces while keeping the existing compositor as a compatibility backend.

## Input boundary

Device drivers translate hardware input into generic Kurogane input events. Consumers receive event streams through a stable interface. Desktop-specific key bindings and focus policy belong to the userspace session/compositor.

Kernel boot/recovery consoles may retain a minimal direct keyboard path, but it must remain separate from desktop policy.

## Networking boundary

Network drivers, packet transport and socket-like/TCP mechanisms belong in the kernel. Browser behavior and command-line network utilities belong in userspace.

Protocol implementations should return typed errors. User-facing components translate those errors into messages. Avoid collapsing every transport/TLS failure into one generic UI error because it makes runtime diagnosis unnecessarily difficult.

## Logging and tests

- Kernel log writes must be serialized so concurrent Ring-3/test output cannot corrupt individual serial lines.
- Test markers identify subsystem contracts, not GUI implementation details.
- Smoke harnesses must fail on the first concrete failed contract and report that contract directly instead of waiting for a generic timeout.
- A test image must expose its source commit/build ID so stale ISO/script combinations are immediately obvious.

## Migration order

1. Finish terminal naming boundary and remove GUI policy from shell frontends.
2. Make VirtualBox smoke diagnostics contract-based and reject stale media/helper combinations.
3. Fix persistent-root contract and TLS/TCP diagnostics.
4. Add executable search-path based command dispatch and move system tools out of the shell core.
5. Introduce generic kernel input/display IPC interfaces.
6. Move dock/window/session policy from the kernel window manager into a Ring-3 Red Flux service.
7. Reduce kernel `applications` framework to boot/recovery compatibility or remove it once userspace supervision is complete.

Every new feature should be reviewed against this boundary before being added to the kernel.

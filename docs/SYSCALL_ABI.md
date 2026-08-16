# KuroganeOS syscall ABI

Status: reserved Foundation v1 contract; transport is not implemented yet.

This document freezes the first machine-level syscall interface so the kernel,
SDK and applications can be developed against one contract. A reserved number
is not evidence that a service works. Until the runtime tests described below
pass, the kernel reports `application_transport_available() == false` and does
not advertise process or file features.

## x86-64 transport

Foundation v1 uses a DPL3 interrupt gate at vector `0x80` and returns with
`iretq`. `SYSCALL/SYSRET` is intentionally deferred until KuroganeOS has safe
per-CPU kernel entry stacks, GS state and canonical-return validation.

| Purpose | Register |
|---|---|
| syscall number | `RAX` |
| argument 1 | `RDI` |
| argument 2 | `RSI` |
| argument 3 | `RDX` |
| argument 4 | `R10` |
| argument 5 | `R8` |
| argument 6 | `R9` |
| result | `RAX` |

All other general-purpose registers are preserved by the v1 transport. `RCX`
and `R11` are not special in the interrupt-gate ABI. A non-negative `RAX` is a
successful value; a negative value is a stable `ku_status_t` error. Unknown
numbers return `KU_STATUS_NOT_SUPPORTED`.

The kernel validates every user pointer for canonical form, overflow, user
range, page permissions and complete byte coverage. A syscall never directly
trusts or dereferences an unchecked userspace pointer.

## Frozen initial numbers

| Number | Name | Arguments | Result | Runtime status |
|---:|---|---|---|---|
| 0 | invalid/reserved | — | `KU_STATUS_NOT_SUPPORTED` | not installed |
| 1 | `KU_SYS_EXIT` | `RDI=status` | does not return | not implemented |
| 2 | `KU_SYS_WRITE` | `RDI=fd`, `RSI=buffer`, `RDX=count` | bytes or error | not implemented |

Assigned numbers are never reused for a different operation. Further numbers
will be appended only with an implemented dispatcher path and tests. The first
three descriptors of every process are planned as `0=stdin`, `1=stdout`, and
`2=stderr`; `WRITE` resolves a real descriptor backend rather than special
casing console output.

## Required runtime proof

The transport becomes available only after QEMU proves all of the following:

- entry originated at CPL3 with `CS=0x23` and `SS=0x1b`;
- `WRITE` copies through validated user mappings and a real FD;
- invalid and cross-page pointers return an error without a kernel panic;
- `EXIT` never returns, publishes an exit code and allows the parent to resume;
- a userspace fault terminates only its process;
- all pages, page tables and kernel-stack resources return to their baseline;
- `/bin/hello` is a validated ELF64 image, not a kernel built-in.

Until those checks pass, SDK wrappers must fail at build/link time or report
the unavailable transport; they must not emulate a successful syscall.

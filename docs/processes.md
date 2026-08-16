# Processes, threads and scheduling

KuroganeOS 2.0 has separate bounded Process and Thread subsystems. A process is
the ownership and address-space boundary; a thread is the schedulable execution
context. The current ELF runtime creates one main thread per process.

## Identities and limits

- `ProcessId`/PID and `ThreadId`/TID are 64-bit generation-checked values.
- PID 1 is reserved for `/system/init` and is created once by `spawn_init`.
- The tables hold at most 16 processes and 16 threads.
- Names are at most 31 bytes; executable paths are at most 255 bytes.
- Reusing a table slot increments its generation, so a stale PID/TID cannot
  accidentally identify a new object.

Each process records PID, parent PID, executable, working directory, main TID,
exit code and observed address-space root. Runtime-owned file handles, heap
allocations and GUI state are held in the process execution context and are not
public Process-table structures.

## States

```text
spawn -> Ready -> Running <-> Ready/Sleeping -> Zombie -> reaped/Empty
                         user fault/exit -----^
```

Thread scheduling adds suspended and terminated lifecycle handling used by
host tests and kernel tasks. Sleep is expressed in PIT ticks. A timer interrupt
wakes due threads and can preempt the running context.

## Context switching

Each thread owns a separate kernel stack. The low-level switch preserves the
required x86-64 register state and changes the stack. Process-thread switching
also binds the process page-table root and updates `TSS.RSP0`, so an interrupt
from Ring 3 lands on that thread's kernel stack. IRQ0 scheduling is bounded and
round-robin; `yield` requests the same scheduler explicitly.

The kernel proves preemption with two non-yielding kernel threads and with an
infinite-loop Ring 3 process running beside an ABI probe. A process cannot keep
the CPU merely by refusing to call `yield`.

## Creation, termination and parent ownership

`spawn(path)` validates an absolute VFS path, reserves a process and thread
slot, and later loads the ELF in that thread. The creator becomes the parent.
`wait(pid, &status)` succeeds only for the parent and only once the child is a
zombie; otherwise it returns would-block, not-found or access-denied. PID 1
restarts the interactive shell when it exits.

`exit` and isolated user exceptions converge on runtime cleanup. Open files are
closed, the owned GUI window is removed, heap/image/stack pages are unmapped,
the private PML4 is destroyed and physical-frame accounting is checked. Kernel
exceptions remain fatal; userspace faults are not.

## Current constraints

There is no `fork`, `exec` replacement, signals, priorities exposed to
userspace, multithreaded process API, inter-process messaging, credentials or
job control. The scheduler is intentionally small round-robin, not a fairness
or real-time scheduler. See [limitations.md](limitations.md).

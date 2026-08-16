# Public syscall ABI

The stable KuroganeOS native ABI is declared only in `sdk/include/kurogane`.
Kernel-private Process, VFS, paging and WindowManager structures are not ABI.
The current ABI major is 1 and the transport is a DPL3 interrupt gate at
vector `0x80`.

```text
RAX = syscall number on entry; signed 64-bit result on return
RDI = argument 1
RSI = argument 2
RDX = argument 3
```

Applications should call SDK wrappers, not emit `int 0x80` directly. A future
transport change can then preserve source compatibility. Callee-clobbered
registers follow the wrapper/compiler contract; the interrupt is a compiler
memory barrier.

## Result and status model

`ku_result_t` is signed 64-bit and carries either a non-negative byte count,
PID, handle, pointer or window ID, or a negative `ku_status_code`.
`ku_status_t` is signed 32-bit. Status values are:

| Value | Name | Meaning |
|---:|---|---|
| 0 | `KU_STATUS_OK` | operation completed |
| -1 | `INVALID_ARGUMENT` | invalid value, pointer, handle or size |
| -2 | `OUT_OF_RANGE` | bounded transfer/range exceeded |
| -3 | `NOT_SUPPORTED` | unsupported operation/number |
| -4 | `NOT_FOUND` | object or child does not exist |
| -5 | `ALREADY_EXISTS` | name/object already exists |
| -6 | `ACCESS_DENIED` | ownership or access rule failed |
| -7 | `OUT_OF_MEMORY` | pages, process, handle or window slot exhausted |
| -8 | `IO_ERROR` | backing VFS/device failure |
| -9 | `WOULD_BLOCK` | no input/event or child still running |
| -10 | `TIMED_OUT` | bounded operation timed out |
| -11 | `INTERRUPTED` | operation interrupted |
| -12 | `BAD_STATE` | subsystem/context is not ready |
| -13 | `VERSION_MISMATCH` | public structure version/size mismatch |
| -14 | `CORRUPT_DATA` | malformed copied structure/data |

## Calls 1-17

| # | Name | Arguments (`RDI`, `RSI`, `RDX`) | Success result | Principal errors |
|---:|---|---|---|---|
| 1 | `EXIT` | signed exit code, 0, 0 | never returns | none |
| 2 | `WRITE` | descriptor, buffer, bytes | bytes written | invalid-argument, out-of-range |
| 3 | `GETPID` | 0, 0, 0 | caller PID | bad-state if no context |
| 4 | `READ` | handle/descriptor, buffer, bytes | bytes read | invalid-argument, would-block, I/O |
| 5 | `OPEN` | path pointer, path bytes, flags | owned file handle | invalid-argument, access-denied, not-found, out-of-memory |
| 6 | `CLOSE` | file handle, 0, 0 | status 0 | invalid-argument, I/O |
| 7 | `ALLOC` | byte count, 0, 0 | page-aligned pointer | invalid-argument, out-of-memory |
| 8 | `FREE` | allocation base, 0, 0 | status 0 | invalid-argument/access-denied |
| 9 | `SLEEP` | PIT ticks, 0, 0 | status 0 | invalid-argument |
| 10 | `YIELD` | 0, 0, 0 | status 0 | bad-state |
| 11 | `GETTID` | 0, 0, 0 | caller TID | bad-state if no context |
| 12 | `SPAWN` | path pointer, path bytes, 0 | child PID | invalid-argument, bad-state, out-of-memory |
| 13 | `WAIT` | child PID, exit-code pointer, 0 | status 0 | would-block, not-found, access-denied |
| 14 | `UI_CREATE` | title, title bytes, options pointer | owned window ID | bad-state, invalid-argument, version-mismatch, out-of-memory |
| 15 | `UI_PRESENT` | window ID, frame pointer, frame size | status 0 | invalid-argument, corrupt-data |
| 16 | `UI_POLL` | window ID, event pointer, event size | status 0 | would-block, invalid-argument |
| 17 | `UI_CLOSE` | window ID, 0, 0 | status 0 | invalid-argument, not-found |

## Detailed rules

### `EXIT` (1)

Terminates the caller with the low signed 32-bit code. All owned files,
allocations, image pages, address-space tables and GUI window are reclaimed.
The parent observes the code through `WAIT`. It never returns.

### `WRITE` (2)

Only descriptor 1 (combined console/serial stdout) is public. The buffer may be
zero length; otherwise all bytes must lie in mapped User pages. At most 16 KiB
may be written per call. Output from one call is serialized against preemption.

### `GETPID`/`GETTID` (3, 11)

Return generation-checked identities of the current runtime context. They do
not expose a pointer to the kernel table.

### `READ` (4)

Descriptor 0 is nonblocking console input and returns `WOULD_BLOCK` if empty.
Other values must be a live handle created by this process. The destination
must be writable User memory and transfers are limited to 16 KiB. Files advance
their private open-file offset.

### `OPEN`/`CLOSE` (5, 6)

The path is an explicit-length absolute byte string, at most 255 bytes, with no
embedded NUL. `KU_OPEN_READ` is the only accepted flag. The returned 64-bit
handle encodes a process-local slot and generation; the kernel decodes it only
inside the calling context. There are 16 file slots per process. Handles cannot
be shared or used after close.

### `ALLOC`/`FREE` (7, 8)

Allocation rounds a nonzero byte size to 4 KiB pages, maps it User/RW/NX and
returns the base. Up to 16 allocations are tracked in the 16 MiB user heap
window. `FREE` accepts only the exact active base owned by the caller.

### `SLEEP`/`YIELD` (9, 10)

`SLEEP` blocks the current thread for a nonzero count of PIT ticks. `YIELD`
voluntarily requests another runnable thread. IRQ0 can preempt a caller that
uses neither operation.

### `SPAWN`/`WAIT` (12, 13)

`SPAWN` copies and validates an absolute executable path, then creates one
child ELF process. `WAIT` requires a writable `int32_t` pointer and parent
ownership. It returns `WOULD_BLOCK` while the child runs and reaps the zombie on
success. The SDK `kuro_spawn_wait` helper performs the yield/retry loop.

### GUI calls (14-17)

`UI_CREATE` allows one live window per process. Titles are 1-32 printable ASCII
bytes. `ku_ui_window_options` must have `structure_size == 20`; bounds must fit
the screen and minimum WindowManager geometry. The returned ID is owned by the
caller.

`UI_PRESENT` requires an exact 800-byte `ku_ui_frame`, `structure_size == 800`,
zero reserved field, at most 12 lines and a NUL in every active 64-byte line.
The entire frame is copied to kernel memory before rendering.

`UI_POLL` requires an exact writable 32-byte `ku_ui_event`. It returns key or
pointer events from a bounded 16-entry copied queue, `WOULD_BLOCK` when empty,
and a close event when WindowManager controls removed the window. `UI_CLOSE`
accepts only the caller's live window ID.

## Global pointer and ownership rules

- arithmetic is checked before converting lengths to `size_t`;
- every covered page must be inside the 64 MiB user region and carry User;
- output buffers additionally require Writable on every page;
- kernel pointers and private structure addresses are never returned;
- file/window handles and heap records are local to the caller;
- only a parent may wait for its child;
- malformed pointers terminate no kernel code path: the call returns an error;
- a later user fault terminates only that process.

The libc names are KuroganeOS-native conveniences. This ABI does not claim
POSIX or Linux syscall compatibility.

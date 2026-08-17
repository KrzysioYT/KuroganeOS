# KuroganeOS Public API Reference

This document describes the **public userspace contract**. Internal kernel
functions are not part of the application ABI.

Target: KuroganeOS **3.3.3-dev**, x86-64 Ring-3.

## Headers

Public headers live under:

```text
sdk/include/kurogane/
```

Typical include:

```c
#include <kurogane/kurogane.h>
```

## Status/result types

Public APIs use `ku_status_t` and `ku_result_t`. Always check a result before
using it as a handle, PID or byte count.

`KU_STATUS_END_OF_STREAM` is used by finite iterators such as directory
enumeration when no more entries are available.

## Process API

```c
#include <kurogane/process.h>
```

Available:

- current PID/TID;
- spawn;
- wait;
- exit;
- sleep/yield.

Each process owns an independent working directory. A child inherits the cwd of
its parent at spawn time, and a relative executable path passed to `spawn` is
resolved from that inherited directory.

Example:

```c
const char path[] = "/apps/hello";
ku_result_t child = ku_process_spawn(path, sizeof(path) - 1U);
if (child > 0) {
    int32_t code = 0;
    while (ku_process_wait((uint64_t)child, &code) == KU_STATUS_WOULD_BLOCK) {
        (void)kuro_sleep(1U);
    }
}
```

## Filesystem API — writable Ring-3 foundation

```c
#include <kurogane/filesystem.h>
```

The public filesystem contract exposes the mutable operations already
implemented by the VFS/FAT32 backend:

```text
open: read / write / append / directory
read / write / seek / close
stat / readdir
create / unlink / rename
mkdir / rmdir
chdir / getcwd
sync
```

Path operations accept absolute and relative paths. Relative paths are resolved
against the calling process cwd; changing cwd in one process does not mutate any
other process or the kernel default path context.

The compatibility helper `ku_file_open(path, size)` remains read-only. Use
`ku_file_open_ex` when write, append or directory flags are required.

Example cwd + relative access:

```c
const char home[] = "/home";
if (ku_chdir(home, sizeof(home) - 1U) == KU_STATUS_OK) {
    char cwd[64];
    size_t required = 0U;
    (void)ku_getcwd(cwd, sizeof(cwd), &required);

    const char name[] = "example.txt";
    (void)ku_file_create(name, sizeof(name) - 1U);
}
```

`ku_getcwd(NULL, 0, &required)` is a supported size query. `required` includes
the trailing NUL byte; the call returns `KU_STATUS_OUT_OF_RANGE` until the
provided buffer is large enough.

Example write:

```c
const char path[] = "/home/example.txt";
const char body[] = "hello from Ring 3\n";

ku_status_t create_status = ku_file_create(path, sizeof(path) - 1U);
if (create_status == KU_STATUS_OK ||
    create_status == KU_STATUS_ALREADY_EXISTS) {
    ku_result_t opened = ku_file_open_ex(
        path,
        sizeof(path) - 1U,
        KU_FILE_OPEN_WRITE);
    if (opened >= 0) {
        ku_result_t written = ku_file_write(
            (ku_file_t)opened, body, sizeof(body) - 1U);
        (void)written;
        (void)ku_file_close((ku_file_t)opened);
        (void)ku_file_sync();
    }
}
```

Seek uses an overflow-checked signed displacement and one of the public origins
`KU_FILE_SEEK_BEGIN`, `KU_FILE_SEEK_CURRENT` or `KU_FILE_SEEK_END`:

```c
uint64_t position = 0U;
ku_status_t status = ku_file_seek(
    file,
    0,
    KU_FILE_SEEK_END,
    &position);
```

The call succeeds only for handles whose file stat advertises
`KU_FILE_FLAG_SEEKABLE`. The resulting absolute offset is returned through the
optional `new_offset` pointer.

Directory enumeration uses a directory handle and returns
`KU_STATUS_END_OF_STREAM` after the last entry:

```c
const char home[] = "/home";
ku_result_t opened = ku_file_open_ex(
    home,
    sizeof(home) - 1U,
    KU_FILE_OPEN_READ | KU_FILE_OPEN_DIRECTORY);
if (opened >= 0) {
    ku_directory_entry entry = {0};
    while (ku_file_readdir((ku_file_t)opened, &entry) == KU_STATUS_OK) {
        /* entry.name, entry.type, entry.size */
    }
    (void)ku_file_close((ku_file_t)opened);
}
```

All path pointers, structure sizes and nested rename paths are validated by the
Ring-3 syscall boundary. Mutation is naturally rejected with
`KU_STATUS_ACCESS_DENIED` when the active root backend is read-only, including
Try/live-package media.

Current limitations: no file-backed mmap, links, ACLs or full Unix permission
model yet.

## IPC — bounded message channels

```c
#include <kurogane/ipc.h>
```

KuroganeOS exposes a fixed-capacity, non-blocking message channel foundation for
process-to-process services. A server binds a short service name, clients connect
by name, and the server accepts each pending connection into a bidirectional
channel.

```text
bind(name) -> endpoint handle
connect(name) -> client channel handle
accept(endpoint) -> server channel handle
send(channel, bytes <= 256)
receive(channel, ku_ipc_message)
close(endpoint or channel)
```

Properties:

- service names are 1-31 characters and limited to ASCII letters, digits,
  `.`, `_` and `-`;
- a message carries at most 256 bytes plus the sender PID;
- queues are bounded; `accept`, `send` and `receive` return
  `KU_STATUS_WOULD_BLOCK` instead of sleeping a kernel thread;
- endpoint and channel handles are generation-checked and owner-checked by PID;
- stale/cross-process handle use is rejected;
- closing/exiting a process releases its IPC ownership and peers observe a
  closed channel state;
- accepted channels survive closure of the listening endpoint.

## Shared memory

```c
#include <kurogane/shared_memory.h>
```

Shared-memory objects provide bounded, true shared physical pages between
explicitly authorized processes. They are not implemented as message copies.

```text
create(size <= 64 KiB) -> owner handle
grant(handle, target_pid)
map(handle) -> process-local writable, NX address
unmap(handle)
close(handle)
```

New pages are zero-filled. The owner must grant access to an exact PID before
that process can map the object. Every mapping points to the same PMM-backed
frames while retaining its own process-local virtual address. Mapping is always
userspace writable and non-executable; the API never exposes arbitrary physical
addresses.

An object is reclaimed only after its open references and active mappings are
gone. Process exit unmaps its Ring-3 views before dropping object ownership, so
another authorized process cannot observe a freed frame through a surviving
mapping.

Example:

```c
ku_result_t created = ku_shm_create(4096U);
if (created >= 0) {
    ku_shm_handle_t shm = (ku_shm_handle_t)created;
    (void)ku_shm_grant(shm, child_pid);
    ku_result_t mapped = ku_shm_map(shm);
    if (mapped >= 0) {
        uint8_t* bytes = (uint8_t*)(uintptr_t)mapped;
        bytes[0] = 42U;
        (void)ku_shm_unmap(shm);
    }
    (void)ku_shm_close(shm);
}
```

## Waitable event foundation

```c
#include <kurogane/event.h>
```

Events are bounded, generation-checked synchronization objects with explicit
PID grants. Both auto-reset and manual-reset semantics are supported.

```text
create(auto/manual, initially_signaled)
grant(handle, target_pid)
signal(handle)
reset(handle)
poll(handle)
wait(handle)
close(handle)
```

`ku_event_poll()` is non-blocking and returns `KU_STATUS_WOULD_BLOCK` while the
event is not signaled. `ku_event_wait()` is the current sleeping wait helper: it
polls and sleeps one scheduler tick between probes, so it does not busy-spin.
This source-level API is intentionally ready for a later direct blocked-thread
wake implementation without changing application call sites.

An auto-reset event consumes one observed signal. A manual-reset event remains
signaled until an authorized process resets it. Event ownership is cleaned up
when a process exits.

Direct wake-by-object from the kernel scheduler and automatic IPC/socket
readiness signaling are still pending; therefore the broader async wait layer is
not yet marked complete in the active roadmap.

## UI / libui

```c
#include <kurogane/ui.h>
#include <kurogane/libui.h>
```

Low-level contract:

```text
create window
present frame/scene
poll event
close window
```

`libui` view types include panel, label, button, input, list item, progress and
separator. Applications should use named key values rather than PS/2 scancodes.

## Live system snapshot — 3.3.3

```c
#include <kurogane/system.h>
```

```c
ku_system_snapshot snapshot = {0};
snapshot.structure_size = sizeof(snapshot);
if (ku_system_get_snapshot(&snapshot) == KU_STATUS_OK) {
    /* snapshot.cpu_percent, ram_percent, disk_percent, gpu_percent */
}
```

Returned fields include CPU activity, RAM totals/percentage, disk activity,
GOP/software-compositor activity and monotonic scheduler uptime ticks.

`gpu_percent` is **not physical GPU-core utilization** in 3.3.3. Hardware 3D
command submission is not enabled yet.

## Desktop shortcuts / pinning — 3.3.3

```c
#include <kurogane/desktop.h>
```

Known application IDs are Home, Terminal, Files, Performance, Kurogane Web,
System Monitor, Settings and About. Home is always pinned. Pin state is
session-local in 3.3.3; persistent desktop configuration will move to the
writable settings service.

## Networking — 3.3.3 transitional public ABI

```c
#include <kurogane/network.h>
```

The kernel path is:

```text
E1000 82540EM -> Ethernet -> ARP -> IPv4 -> DHCP -> DNS A -> TCP
```

The public snapshot exposes readiness, physical-interface/DHCP state, IPv4
address, gateway, DNS and RX/TX byte counters.

3.3.3 also exposes one bounded HTTP/1.0 GET over TCP port 80. Limits remain:

- no TLS/HTTPS yet;
- response buffer maximum 4096 bytes;
- synchronous bounded request;
- not a general socket API;
- simple in-order TCP receive path.

This is enough for the first native `Kurogane Web` bootstrap but **not enough to
port Chromium**. Chromium requires asynchronous sockets, TLS, threads, timers,
filesystem/process integration and a much broader libc/POSIX platform layer.

Applications must never include `kernel/net/*` directly.

## Audio — bounded Ring-3 PCM foundation

```c
#include <kurogane/audio.h>
```

The reference backend is Intel ICH AC'97 (`8086:2415`). Ring 3 can query state,
set master volume/mute and submit one bounded PCM block through the public SDK.

The playback contract is signed 16-bit little-endian, stereo interleaved,
48 kHz and maximum 1024 frames per submitted block. Accepted samples are copied
into kernel-owned DMA memory before the syscall returns. Playback is currently
exclusive and owned by the submitting PID.

This is not yet the final multi-stream audio service. Stream handles, mixing,
format conversion/resampling, capture and Intel HDA remain future work.

## Graphics / Direct3D

3.3.3 registers a PCI display-class capability driver and distinguishes UEFI GOP
scanout, the software compositor and hardware 3D capability. Hardware 3D remains
false until a real GPU command-submission backend exists.

Direct3D 9/11/12 are **not yet a supported application ABI**. See
[`../GRAPHICS_COMPATIBILITY.md`](../GRAPHICS_COMPATIBILITY.md).

## Syscall table additions in 3.3.3

Existing syscall numbers 1-17 remain unchanged. Entries added append-only are:

```text
18  KU_SYS_SYSTEM_SNAPSHOT
19  KU_SYS_DESKTOP_PIN
20  KU_SYS_NET_STATUS
21  KU_SYS_HTTP_GET
22  KU_SYS_AUDIO_STATUS
23  KU_SYS_AUDIO_SET
24  KU_SYS_FS_STAT
25  KU_SYS_FS_READDIR
26  KU_SYS_FS_CREATE
27  KU_SYS_FS_UNLINK
28  KU_SYS_FS_RENAME
29  KU_SYS_FS_MKDIR
30  KU_SYS_FS_RMDIR
31  KU_SYS_FS_SYNC
32  KU_SYS_FS_SEEK
33  KU_SYS_AUDIO_PLAY_PCM16
34  KU_SYS_AUDIO_POLL
35  KU_SYS_AUDIO_STOP
36  KU_SYS_FS_CHDIR
37  KU_SYS_FS_GETCWD
38  KU_SYS_IPC_BIND
39  KU_SYS_IPC_CONNECT
40  KU_SYS_IPC_ACCEPT
41  KU_SYS_IPC_SEND
42  KU_SYS_IPC_RECEIVE
43  KU_SYS_IPC_CLOSE
44  KU_SYS_SHM_CREATE
45  KU_SYS_SHM_GRANT
46  KU_SYS_SHM_MAP
47  KU_SYS_SHM_UNMAP
48  KU_SYS_SHM_CLOSE
49  KU_SYS_EVENT_CREATE
50  KU_SYS_EVENT_GRANT
51  KU_SYS_EVENT_SIGNAL
52  KU_SYS_EVENT_RESET
53  KU_SYS_EVENT_POLL
54  KU_SYS_EVENT_CLOSE
```

`KU_SYS_WRITE` remains syscall 2 and accepts generation-checked file handles in
addition to stdout/stderr. `KU_SYS_OPEN` remains syscall 5 and accepts the
write/append/directory flags. This preserves source and syscall-number
compatibility for existing applications.

Use SDK wrappers instead of hardcoding syscall numbers.

## Adding a public API

1. define the fixed-width userspace structure under `sdk/include/kurogane/`;
2. append a syscall number without renumbering older calls;
3. add an SDK wrapper;
4. validate every Ring-3 pointer and nested buffer;
5. validate `structure_size`, flags and enums;
6. bound allocation/work/time performed by the syscall;
7. define ownership and blocking semantics before freezing the ABI;
8. add runtime/build validation;
9. update this document.

Never trust a pointer, size, enum or nested address supplied by Ring-3.

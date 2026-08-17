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

Executable spawn paths remain absolute in 3.3.3. Filesystem data paths can be
absolute or relative to the calling process cwd, as described below.

## Filesystem API — writable Ring-3 foundation

```c
#include <kurogane/filesystem.h>
```

The public filesystem contract exposes:

```text
open: read / write / append / directory
read / write / seek / close
stat / readdir
create / unlink / rename
mkdir / rmdir
chdir / getcwd
sync
```

The compatibility helper `ku_file_open(path, size)` remains read-only. Use
`ku_file_open_ex` when write, append or directory flags are required.

### Process-local current directory

Every Ring-3 runtime context starts with cwd `/` and owns an independent VFS
`PathContext`. `chdir` changes only the calling process; it never mutates the
kernel/global root context or another process' cwd.

```c
const char home[] = "/home";
ku_status_t status = ku_file_chdir(home, sizeof(home) - 1U);
if (status == KU_STATUS_OK) {
    char cwd[256];
    ku_result_t length = ku_file_getcwd(cwd, sizeof(cwd));
    if (length >= 0) {
        /* cwd == "/home"; length excludes the trailing NUL */
    }
}
```

After changing cwd, filesystem data operations accept relative paths:

```c
const char name[] = "notes.txt";
ku_result_t opened = ku_file_open_ex(
    name,
    sizeof(name) - 1U,
    KU_FILE_OPEN_READ | KU_FILE_OPEN_WRITE);
```

Path canonicalization remains inside the VFS. Relative `.` and `..` components
are normalized against the process path context and cannot bypass an active
VFS root/chroot boundary. Path length/depth limits still apply after
canonicalization.

### Mutable file operations

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

Current filesystem limitations: no file-backed mmap, links, ACLs or full Unix
permission/ownership model yet.

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

Returned fields include:

- CPU activity estimate from scheduler/timer counters;
- RAM percentage and total/free physical memory;
- disk activity from completed block transfers;
- GPU/GFX activity from the current GOP/software-compositor submission path;
- uptime ticks.

`gpu_percent` is **not physical GPU-core utilization** in 3.3.3. Hardware 3D
command submission is not enabled yet.

## Desktop shortcuts / pinning — 3.3.3

```c
#include <kurogane/desktop.h>
```

Known application IDs are Home, Terminal, Files, Performance, Kurogane Web,
System Monitor, Settings and About.

Example toggle:

```c
ku_desktop_pin_request request = {0};
request.structure_size = sizeof(request);
request.app_id = KU_DESKTOP_APP_BROWSER;
request.action = KU_DESKTOP_PIN_TOGGLE;
if (ku_desktop_pin(&request) == KU_STATUS_OK) {
    /* request.pinned contains the new state */
}
```

Home is always pinned. Pin state is session-local in 3.3.3; persistent desktop
configuration will move to the writable settings service.

## Networking — 3.3.3 transitional public ABI

```c
#include <kurogane/network.h>
```

The kernel path is:

```text
E1000 82540EM
 -> Ethernet
 -> ARP
 -> IPv4
 -> DHCP
 -> DNS A
 -> TCP
```

### Network status

```c
ku_network_status status = {0};
status.structure_size = sizeof(status);
ku_status_t result = ku_network_get_status(&status);
```

The snapshot exposes readiness, physical-interface/DHCP state, IPv4 address,
gateway, DNS and RX/TX byte counters.

### Bounded HTTP GET

3.3.3 exposes the first application-facing Internet transport:

```c
char response[4096];
ku_http_request request = {0};
request.structure_size = sizeof(request);
strlcpy(request.host, "example.com", sizeof(request.host));
strlcpy(request.path, "/", sizeof(request.path));
request.output = response;
request.output_capacity = sizeof(response);
ku_status_t result = ku_http_get(&request);
```

Limits in DEV BETA:

- HTTP on TCP port 80 only;
- no TLS/HTTPS yet;
- response buffer maximum 4096 bytes;
- synchronous bounded request;
- not a general socket API;
- simple in-order TCP receive path.

This is enough for the first native `Kurogane Web` browser but **not enough to
port Chromium**. Chromium requires asynchronous sockets, TLS, threads, timers,
filesystem/process integration and a much broader libc/POSIX platform layer.

Applications must never include `kernel/net/*` directly.

## Audio — bounded Ring-3 PCM foundation

```c
#include <kurogane/audio.h>
```

The reference backend is Intel ICH AC'97 (`8086:2415`). Ring 3 can query state,
set master volume/mute and submit one bounded PCM block through the public SDK.

The playback contract is fixed in 3.3.3:

```text
signed 16-bit little-endian
stereo / interleaved L,R
48 kHz
maximum 1024 frames per submitted block
```

Example:

```c
int16_t samples[2U * 256U];
/* fill 256 stereo frames */
ku_status_t status = ku_audio_play_pcm16_stereo(samples, 256U);
if (status == KU_STATUS_OK) {
    while (ku_audio_poll() == KU_STATUS_WOULD_BLOCK) {
        (void)ku_yield();
    }
}
```

Accepted samples are copied into kernel-owned DMA memory before the submit
syscall returns. Playback is currently exclusive and owned by the submitting
PID; another process cannot poll or stop that playback. Process cleanup stops
DMA automatically if the process exits while it still owns playback.

This is not yet the final multi-stream audio service. Stream handles, mixing,
format conversion/resampling, capture and Intel HDA remain future work.
Applications must never program AC'97 DMA or I/O ports directly.

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

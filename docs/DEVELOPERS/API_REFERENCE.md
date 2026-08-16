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

## Filesystem API

```c
#include <kurogane/filesystem.h>
```

Current public stable core:

```text
open read-only file
read
close
```

The kernel VFS supports more internally, but an internal capability is not a
public syscall until pointer, permission and ownership semantics are defined.

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

## Audio

The kernel has an Intel ICH AC'97 (`8086:2415`) PCM backend for the reference
VirtualBox profile. A stable Ring-3 streaming API is still pending; applications
must not program AC'97 DMA or I/O ports directly.

## Graphics / Direct3D

3.3.3 registers a PCI display-class capability driver and distinguishes UEFI GOP
scanout, the software compositor and hardware 3D capability. Hardware 3D remains
false until a real GPU command-submission backend exists.

Direct3D 9/11/12 are **not yet a supported application ABI**. See
[`../GRAPHICS_COMPATIBILITY.md`](../GRAPHICS_COMPATIBILITY.md).

## Syscall table additions in 3.3.3

Existing syscall numbers 1-17 remain unchanged. New append-only entries are:

```text
18  KU_SYS_SYSTEM_SNAPSHOT
19  KU_SYS_DESKTOP_PIN
20  KU_SYS_NET_STATUS
21  KU_SYS_HTTP_GET
```

Use SDK wrappers instead of hardcoding these numbers.

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

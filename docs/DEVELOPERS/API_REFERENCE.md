# KuroganeOS Public API Reference

This document describes the **public userspace contract**. Internal kernel
functions are not part of the application ABI.

Target: KuroganeOS 3.3.1-dev, x86-64 Ring-3.

## Headers

Main public headers live under:

```text
sdk/include/kurogane/
```

Typical include:

```c
#include <kurogane/kurogane.h>
```

## Status/result types

Public APIs generally use:

```c
ku_status_t
ku_result_t
```

Negative/encoded status results must be checked before treating a result as a
handle, PID or count.

## Process API

Header:

```c
#include <kurogane/process.h>
```

Available concepts:

- current PID/TID;
- spawn;
- wait;
- exit;
- sleep/yield helpers through `kurogane.h`.

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

## Memory API

Public allocation wrappers use kernel-backed per-process mappings. Do not pass
raw physical addresses from applications.

Use:

```text
allocate
free
```

Allocation limits are deliberately bounded in DEV BETA.

## Filesystem API

Header:

```c
#include <kurogane/filesystem.h>
```

Stable core:

```text
open read-only file
read
close
```

The VFS backend itself supports more operations, but a kernel capability does
not automatically become a public syscall. Writable/public metadata functions
are added only with pointer/permission validation.

## UI API

Headers:

```c
#include <kurogane/ui.h>
#include <kurogane/libui.h>
```

Current low-level window contract:

```text
create window
present frame/scene
poll event
close window
```

Current events:

```text
close
key
pointer
```

Named keys include:

```text
Escape
Backspace
Tab
Enter
Home
Arrow Up/Down/Left/Right
Page Up/Down
Insert
Delete
```

Do not depend on raw PS/2 scancodes in applications.

## libui scene API

`libui` provides a compatibility scene/view layer over the current UI transport.
Available view types include:

```text
panel
label
button
input
list item
progress
separator
```

Common functions:

```text
kui_scene_initialize
kui_scene_set_palette
kui_scene_add
kui_scene_set_text
kui_scene_set_flags
kui_scene_select
kui_scene_select_next
kui_scene_present
kui_next_event
```

Flow helpers:

```text
kui_flow_begin
kui_flow_panel
kui_flow_label
kui_flow_button
kui_flow_input
kui_flow_list_item
kui_flow_progress
kui_flow_separator
```

## Network API — 3.3.1 foundation

3.3.1 introduces/defines a small application-facing contract around the kernel
network service. The intended public operations are deliberately narrower than
a raw network stack:

```text
query interface readiness/configuration
resolve an IPv4 A record
ping IPv4 address/gateway
gather network stats
```

Full socket handles are a later ABI step. Applications must not call
`kernel/net/service.*` directly.

## Audio API — 3.3.1 foundation

The first public audio contract targets playback of bounded PCM blocks.
Reference format:

```text
PCM S16LE
stereo
48000 Hz
```

Expected operations:

```text
audio status/capabilities
submit PCM frames
stop/reset playback
```

The implementation remains bounded and validates the user buffer before DMA
submission/copy.

## Graphics API

The current application graphics model is UI/window oriented. A native graphics
resource API is planned before any claim of Direct3D compatibility.

See [`../GRAPHICS_COMPATIBILITY.md`](../GRAPHICS_COMPATIBILITY.md).

## Syscall stability

Applications should use SDK wrappers. Do **not** hardcode syscall numbers unless
you are working on the ABI itself.

Reason:

```text
source API -> SDK wrapper -> syscall transport
```

allows the transport to change later without forcing every application to
rewrite inline assembly.

## Adding a new public API

For a new syscall/API:

1. define the userspace structure in `sdk/include/kurogane/`;
2. keep structures fixed-width and ABI-checkable;
3. add a wrapper;
4. implement kernel validation;
5. validate every pointer/range;
6. reject unknown flags/version/structure sizes;
7. add a test;
8. update this document.

Never trust `size`, pointer or enum values supplied by Ring-3.

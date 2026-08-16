# Writing Applications for KuroganeOS

This guide targets KuroganeOS 3.3.1-dev and the current x86-64 Ring-3 SDK.

## 1. Minimal application

Create `hello.c`:

```c
#include <stdio.h>

int main(void) {
    puts("Hello from KuroganeOS!");
    return 0;
}
```

KuroganeOS applications are linked against the project SDK and use the
KuroganeOS syscall ABI. They are not Linux binaries and do not use glibc.

## 2. Where the SDK lives

After building the SDK:

```text
build/sdk/sysroot/usr/include/
build/sdk/sysroot/usr/lib/
```

Important libraries:

```text
libc.a
libkurogane.a
libui.a
crt0.o
kurogane-user.ld
```

## 3. Build an application manually

The easiest development route is to copy the pattern from
`sdk/examples/hello/` or use the host helper scripts.

### macOS

```bash
./scripts/build-app-macos.sh hello.c -o hello --install
```

Then rebuild/stage the media and boot KuroganeOS.

### Linux

Use the SDK compiler and linker pattern from `scripts/build-sdk.sh`. A dedicated
Linux app helper can be added using the same ABI; the produced executable must
remain an x86-64 ELF `ET_EXEC` image.

### Windows

Use the repository-local `x86_64-elf-*` toolchain installed from the Windows
build package and follow the same linker script/SDK library order as
`build-sdk.ps1`.

## 4. Run an application

Installed user applications normally live under:

```text
/apps/<name>
```

From Flux Terminal:

```text
run hello
```

or when using an absolute path through APIs/tools that accept one:

```text
/apps/hello
```

## 5. Process model

Each application runs in Ring-3 with:

- its own address space;
- its own user stack;
- a PID/TID;
- validated syscall arguments;
- bounded handle/allocation tables;
- exception isolation from the kernel.

An application must never assume that kernel pointers are available.

## 6. Process API

Useful public operations include:

```c
#include <kurogane/process.h>
#include <kurogane/kurogane.h>
```

Typical operations:

```text
get PID
spawn another ELF
wait for a child
sleep
yield
exit
```

Use SDK wrappers rather than embedding `int 0x80` in applications.

## 7. Filesystem API

Read-only open/read/close is stable in the current public ABI. Writable VFS
capabilities are being expanded incrementally.

Never include `kernel/fs/*.hpp` from a Ring-3 program. Kernel headers are not a
public userspace API.

## 8. GUI applications

Use `libui` and public UI headers:

```c
#include <kurogane/libui.h>
#include <kurogane/ui.h>
```

See [`GUI_APPLICATIONS.md`](GUI_APPLICATIONS.md).

## 9. Networking

The **system/kernel** in 3.3.1 already has E1000, DHCP, IPv4, DNS and basic
transport functionality and can use the VirtualBox 82540EM + NAT profile.

The **application SDK** does not yet expose a stable socket/DNS/ping ABI. Do not
import `kernel/net/*` into a Ring-3 application as a workaround. The public
network API is deliberately waiting for an asynchronous handle/event design so
a DNS request does not become a long blocking kernel syscall.

See [`API_REFERENCE.md`](API_REFERENCE.md) and
[`../NETWORKING.md`](../NETWORKING.md).

## 10. Audio

3.3.1 ships the Intel ICH AC'97 kernel driver and a bounded PCM16 stereo DMA
backend. It does **not** yet ship a stable Ring-3 audio stream API.

Do not program PCI, BARs or AC'97 ports from an application. When the public
audio stream API lands, it will sit between the application and this driver.

Reference backend format:

```text
signed PCM16 little-endian
stereo
48000 Hz
```

See [`API_REFERENCE.md`](API_REFERENCE.md) and [`../AUDIO.md`](../AUDIO.md).

## 11. Error handling

Most low-level APIs return `ku_status_t` or `ku_result_t`.

Always check errors:

```c
ku_status_t status = kuro_yield();
if (status != KU_STATUS_OK) {
    /* handle the failure */
}
```

Do not assume that `WOULD_BLOCK` is fatal. For event/process style APIs it often
means "try again later".

## 12. Rules for code that should be merged

- no arbitrary Ring-0 escape hatch;
- validate every userspace pointer in the kernel;
- no W+X userspace segments;
- no unresolved ELF symbols;
- use public SDK headers in applications;
- keep hardware-specific code in drivers/kernel services;
- do not publish a blocking placeholder syscall just to reserve an API number;
- document a new public API in `API_REFERENCE.md`;
- add a test or runtime marker for new security-sensitive behavior.

## 13. Next example to study

Good code-reading order:

```text
sdk/examples/hello/main.cpp
userspace/apps/hello/main.S
userspace/gui/about/main.c
userspace/gui/terminal/main.c
sdk/src/libkurogane.c
kernel/user/runtime.cpp
```

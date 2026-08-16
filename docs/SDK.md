# Native SDK

KuroganeOS 2.0 provides a public native SDK for static x86-64 ELF64 programs.
It exports no kernel-private structure and claims no POSIX/Linux/glibc
compatibility.

## Sysroot

```powershell
.\scripts\build-sdk.ps1
```

Outputs under `build/sdk/sysroot` include public headers in `usr/include` and
`crt0.o`, `libc.a`, `libkurogane.a`, `libui.a` plus `kurogane-user.ld` in
`usr/lib`. Every built app is checked for x86-64 ET_EXEC, undefined symbols,
executable stack and W+X segments.

`crt0` defines `_start`, aligns the stack, calls `main`, then `exit`. KuroLibC
provides memory/string primitives, `malloc/free`, limited `printf/puts/putchar`
and read-only `open/close/read/write`. `printf` supports `%% c s d u x p` only.
`libkurogane` provides sleep/yield/spawn-wait helpers; `libui` initializes and
presents copied frames/events.

## External Hello World

```cpp
#include <stdio.h>

int main() {
    printf("Hello from external Kurogane application\n");
    return 0;
}
```

`sdk/examples/hello/main.cpp` builds to `build/sdk/examples/hello`, is copied
to `/apps/external` in base and installer images, and runs via the Ring 3 shell
command `external` using only the public ABI.

## Standalone project

```powershell
python .\scripts\create-sdk-project.py --list-templates
python .\scripts\create-sdk-project.py --template console `
  --name my-app --output .\build\my-app
```

Console, GUI and service templates link runnable static ET_EXEC files. Driver
generation is rejected because no public loadable-driver ABI exists. Build in
WSL, for example:

```sh
make -C build/my-app CXX=g++ \
  KUROGANE_SYSROOT=/mnt/e/KuroganeOS/build/sdk/sysroot
```

Copy `build/my-app/build/my-app` into an image overlay as `/apps/my-app`, then
repackage and launch it through `SPAWN`. ABI major 1 fixes syscall/status/handle
and public structure contracts; callers must initialize sizes/reserved fields.

There is no dynamic loader, shared-library ABI, C++ standard library,
exceptions/RTTI, socket API, threads API, PE or `.kex` support in 2.0.

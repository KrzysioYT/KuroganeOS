# sdk-gui-probe

This is a runnable KuroganeOS `gui` SDK scaffold. It links statically
against the public CRT, libc, libkurogane and libui ABI and requires these
runtime features: `processes, gui, input`.

Build against a generated SDK sysroot:

```sh
make KUROGANE_SYSROOT=/path/to/kurogane/sysroot
```

The output `build/sdk-gui-probe` is an ELF64 ET_EXEC image. Copy it to the
system filesystem (normally `/apps/sdk-gui-probe`) and launch it from the
userspace shell. This SDK is KuroganeOS-native and does not claim POSIX binary
compatibility.

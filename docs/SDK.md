# KuroganeOS experimental SDK

The current SDK is an early ABI foundation, not a complete application
runtime. It provides architecture-neutral public scalar types, shared status
codes, ABI version negotiation and feature discovery. Kernel-private headers
are deliberately excluded.

Build and validate the sysroot from WSL:

```sh
./scripts/build-sdk.sh
./scripts/test.sh
```

The generated sysroot is placed in `build/sdk/sysroot`. The `abi-inspect`
example is compiled only as a freestanding object because KuroganeOS does not
yet provide ring-3 processes, application startup objects or a system-call
transport. Applications must treat every bit absent from
`ku_abi_descriptor.available_features` as unavailable.

## Compatibility contract

- ABI version is encoded as `major << 16 | minor`.
- A major mismatch is rejected.
- Structures carry an explicit size where future extension is expected.
- Reserved fields must be written as zero and ignored when read.
- Public handles are unsigned 64-bit values; zero is invalid.
- Errors are negative `ku_status_t` values and success is non-negative.

No syscall numbers are published yet. Assigning numbers before a real kernel
dispatcher and user-pointer validation exist would create a misleading ABI.

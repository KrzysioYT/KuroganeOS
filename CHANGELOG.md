# Changelog

## Unreleased

- Added the experimental public application ABI v1 foundation, shared status
  codes, a generated SDK sysroot and a compile-only external consumer.
- Added the kernel ABI descriptor and `abi` shell command. It explicitly
  reports that the ring-3/syscall transport and public services are unavailable.
- Added an ABI layout/validation regression test and QEMU shell coverage.
- Added allocation-free structured kernel logging with runtime severity
  filtering, module names, CPU/PID/TID context and serial/framebuffer output.
- Added structured boot, memory, scheduler, interrupt and fatal exception
  diagnostics, including the fault address for page faults.
- Added reproducible hosted tests for allocator, RAMFS, scheduler and network.
- Isolated QEMU logs by run name.
- Added audited architecture, capability, testing, installation and
  compatibility documentation.
- Verified clean freestanding build and UEFI QEMU smoke/system boots.

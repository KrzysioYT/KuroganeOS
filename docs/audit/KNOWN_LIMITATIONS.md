# Known limitations (historical baseline)

This file records the pre-Foundation baseline. Current limitations are maintained in `../KNOWN_ISSUES.md`.

- Kernel and applications share ring 0 and one address space.
- The scheduler runs callbacks cooperatively; it is not a process/thread model.
- RAMFS is volatile and there is no VFS, disk driver, or partition support.
- Networking is an in-memory protocol/loopback implementation without a NIC.
- GUI applications are kernel-resident and there is no compositor or isolation.
- Fatal exception output includes severity, module, execution context, RIP and
  the fault address for page faults. It still has no recursive-panic guard,
  register dump, symbolized backtrace or stack unwinder.
- SMP, APIC, ACPI, audio, USB, security accounts and permissions are absent.
- The ISO is a bootable live smoke image, not an installation medium.
- Secure Boot is unsupported. Real-hardware compatibility is untested.

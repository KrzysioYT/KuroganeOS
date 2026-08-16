# Current limitations

KuroganeOS 2.0 is an educational Desktop Alpha, not a secure or production
operating system. The list below is part of the release contract.

## Kernel

- single CPU only; APIC topology is discovered but interrupt routing keeps the
  tested PIC fallback and application processors are not started;
- fixed bounded tables (16 processes, 16 threads, 12 windows);
- no loadable modules, signals, IPC facility, credentials or service manager;
- no panic recovery, kernel debugger integration or live update.

## Memory and security

- no ASLR, demand paging, copy-on-write, swap, shared memory or mmap;
- heap allocation is page-granular and monotonic in virtual-address choice;
- Ring3, NX, CR0.WP, pointer/page validation, ELF W+X rejection, owned handles
  and private CR3s are implemented, but the system has no formal security
  review, users/ACLs, sandbox policy, cryptography or secure boot;
- denial of service remains possible through bounded resource exhaustion.

## Hardware

- qualification is QEMU q35/EDK2, not general PCs;
- only AHCI SATA, Intel 82540EM/E1000, PS/2 and a constrained xHCI HID keyboard
  path are implemented;
- no NVMe, VirtIO, Wi-Fi, Bluetooth, audio, GPU acceleration, USB storage,
  USB mouse or power management/shutdown driver.

## Filesystem and storage

- writable FAT32 mutation targets ASCII 8.3 names; no journaling or crash-safe
  transactions;
- no permissions, ownership, symlinks, hard links, file locks, mmap or stable
  writable userspace filesystem ABI;
- installed layout is fixed to GPT + 64 MiB ESP + FAT32 root;
- installer intentionally rejects nonblank disks and has no partition-preserve,
  upgrade, encryption or dual-boot mode.

## Networking

- no userspace sockets, IPv6, firewall, TLS, fragmentation/reassembly, Wi-Fi or
  general TCP stack;
- TCP is a bounded active validation client, not a complete transport;
- external DNS/HTTP/ICMP results depend on the host/QEMU network.

## GUI and applications

- software full redraw, fixed bitmap text, no GPU compositor, Unicode input,
  clipboard, resize, accessibility, multi-monitor or desktop file associations;
- one public GUI window per process; frames contain fixed text lines and one
  progress value rather than an extensible widget protocol;
- Settings changes only its real per-session theme and does not persist;
- Terminal has a small native command set, no pipes, redirection, jobs or TTY
  control; the separate console shell remains for CLI use.

## Compatibility

- x86-64 ELF64 ET_EXEC only; no PE/`.exe`, Linux ABI, dynamic loader, shared
  libraries or custom `.kex` format;
- libc names are POSIX-like conveniences but are not POSIX/glibc compatible;
- no C++ standard library, exceptions or RTTI runtime.

## Performance

- single-core round-robin scheduler and polling budgets are tuned for tests;
- GUI redraws whole visible windows; network/xHCI work is polled;
- FAT32 and heap algorithms favor bounded correctness over throughput;
- no benchmark or real-time guarantee is claimed.

## Recovery and support boundary

Keep images disposable. Use safe mode for bounded diagnostics when PID1,
network or desktop startup is inappropriate. Never attach a physical host disk
to installer or writable-storage tests. A feature absent from
[hardware.md](hardware.md) must be treated as unsupported even if firmware can
enumerate it.

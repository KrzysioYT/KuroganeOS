# Memory management

## Physical memory and heap

The loader-supplied UEFI memory map seeds a bitmap physical-frame allocator.
Reserved/firmware/kernel regions are never handed out. Allocation/free paths
check alignment, range, double free and accounting. The kernel heap is a
bounded allocator built from explicitly reserved memory and reports total,
used, free bytes and allocation count to diagnostics.

## Four-level virtual memory

The kernel clones the active UEFI PML4 into an owned kernel address space,
retaining required loader/framebuffer/MMIO mappings. Page-table operations
validate canonical addresses, alignment, overflow, conflicts and effective
permissions at every level. CR0.WP makes supervisor writes obey read-only PTEs;
EFER.NXE enables non-executable pages.

Each process gets a new PML4. Kernel mappings are shared structurally but remain
supervisor-only. Only explicitly mapped user pages carry the User bit. Destroy
logic knows which tables are owned and never frees external firmware tables.

## User virtual layout

```text
0x0000400000000000  +----------------------------------+
                    | ELF64 PT_LOAD image (max 256 pp) |
                    | RX text / R data / RW+NX data    |
                    +----------------------------------+
0x0000400002000000  | stack top                        |
                    | 8 writable, NX pages             |
                    | unmapped guard below             |
                    +----------------------------------+
0x0000400002800000  | monotonic userspace heap         |
                    | allocations: writable + NX       |
0x0000400003800000  +----------------------------------+
                    | reserved                         |
0x0000400003fff000  | fault-return trampoline (RX)     |
0x0000400004000000  +----------------------------------+ user region end
```

The complete user window is 64 MiB. The loaded executable is limited to 256
mapped pages and 512 KiB file size; stack size is 32 KiB. The heap has 16
tracked allocation records and currently grows monotonically within its 16 MiB
window. `free` unmaps owned pages but does not compact the address cursor.

## Permissions and pointer validation

ELF segments are rejected if they overlap, escape the user window, overflow,
combine writable and executable permissions, or have an entry point outside an
executable segment. Stack and heap are NX. The kernel never trusts the numerical
range alone: every syscall walks all covered pages and requires User plus the
appropriate Writable permission before reading or writing. Zero-length buffers
are accepted without dereference where the syscall contract permits it.

MMIO mappings for APIC, AHCI, E1000, xHCI and framebuffer are supervisor-only
and use appropriate cache policy. Userspace cannot issue page-table or hardware
mapping operations.

## Known limitations

There is no demand paging, copy-on-write, swap, ASLR, memory-mapped file API,
shared memory, user page-fault handler or overcommit. The physical allocator and
tables are sized for the educational reference target, not large SMP systems.

# Architecture

KuroganeOS 2.0 is qualified on a single-CPU QEMU q35 guest with EDK2.

```text
UEFI firmware
    v
BOOTX64.EFI -- validates ELF64 PIE and boot protocol v3
    v
Kernel (Ring 0)
 |-- physical/virtual memory and heap
 |-- Process/Thread scheduler and int 0x80 ABI
 |-- VFS -- RAMFS/FAT32 -- GPT/PartitionDevice -- AHCI
 |-- PCI device model -- AHCI/E1000/xHCI
 |-- Ethernet/ARP/IPv4/ICMP/UDP/TCP/DHCP/DNS
 `-- framebuffer + unified input + WindowManager
             v
       Ring 3, private CR3
             v
          PID 1 init
             v
      shell and ELF64 applications
```

## Boot flow

EDK2 loads `EFI/BOOT/BOOTX64.EFI`. The loader obtains GOP and the UEFI memory
map, validates/maps the x86-64 PIE kernel, applies only supported
`R_X86_64_RELATIVE` relocations, optionally loads the bounded installer package,
and exits boot services. Boot protocol v3 supplies framebuffer, memory map,
ACPI pointer, flags and installer payload. The kernel rejects invalid versions,
sizes, ranges and flag/payload combinations.

The kernel establishes logging, GDT/TSS/IST, IDT, memory, drivers, persistent
root, self-tests, interrupts and networking. Normal boot starts `/system/init`
as PID 1. Desktop boot also initializes WindowManager and spawns `/gui/*`.
Safe/diagnostics boot avoids normal PID 1 and exposes the emergency Ring 0 shell.

## Memory and privilege

The kernel is Ring 0. Every ELF process receives a cloned PML4 whose kernel
mappings remain supervisor-only and whose 64 MiB user window is private.
Executable pages are RX, data/stack/heap are NX, a stack guard stays unmapped,
CR0.WP is enabled and EFER.NXE enforces NX. Interrupts from Ring 3 use that
thread's TSS Ring 0 stack. A user exception terminates only its process through
a controlled return trampoline; a kernel exception remains fatal.

## Processes and scheduling

Process and Thread tables are bounded, independent and generation checked.
Every process currently has one main thread. PIT IRQ0 drives round-robin
preemption; sleep and yield use the same thread state machine. Context switches
change stack, CR3 and TSS.RSP0. PID 1 supervises the console shell, and only a
parent may wait for and reap its zombie child.

## Syscalls and ELF

A DPL3 gate at `int 0x80` carries ABI v1 (`RAX` number/result, three arguments
in `RDI/RSI/RDX`). The kernel identifies the caller by private CR3 and validates
full pointer ranges, PTE permissions, structures, handles and ownership.

The ELF loader accepts bounded x86-64 `ET_EXEC` only. It checks all header/table
arithmetic, segment ranges and overlap, entry location and W+X rejection before
mapping. There is no dynamic loader, shared object or PE compatibility.

## Storage, devices and network

AHCI exposes checked 512-byte block devices. GPT signatures, CRCs, ranges and
overlap are validated before PartitionDevice creation. VFS mounts writable
FAT32 on the normal root, with RAMFS fallback. FAT32 mirrors allocation updates
and flushes the block device. Installer media create protective MBR,
primary/backup GPT, ESP and root from a bounded package.

The device table records platform, PCI and child relationships. ACPI/MADT maps
Local/I/O APIC identity but the active qualified interrupt route remains the
8259 PIC. E1000 owns DMA rings; frames pass through Ethernet, ARP and IPv4 to
ICMP/UDP/minimal TCP. DHCP installs address/route/DNS, with bounded polling.

## GUI architecture

GOP is software rendered. PS/2 and xHCI HID input enter one event queue.
WindowManager owns 12 windows, z-order, focus, drag, taskbar and window state.
It redraws visible windows when dirty; there is no GPU compositor.

One GUI process owns at most one public window. `UI_PRESENT` copies an exact
fixed frame into kernel memory before drawing; callbacks retain no userspace
pointer. Key/pointer/close events are copied into a bounded per-process queue.
Process cleanup closes the window and releases all other resources.

## Ownership boundary

Kernel VFS handles, process slots, page tables, network buffers and window slots
are private. Public handles combine process-local slot and generation. Files,
heap records, child waits and windows are checked against the caller; all are
closed/unmapped on normal exit or isolated user fault.

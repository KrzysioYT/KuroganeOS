# Contributing to the KuroganeOS Kernel

Kernel work is the most privileged part of KuroganeOS. Bugs here can corrupt
memory, storage or user isolation.

## Before editing kernel code

You should understand at least:

- x86-64 privilege levels;
- UEFI boot basics;
- page tables and virtual memory;
- interrupts/exceptions;
- ELF loading;
- PCI/MMIO/I/O ports;
- DMA ownership;
- synchronization/preemption.

If not, start with Ring-3 application development first.

## Repository areas

```text
boot/efi/              UEFI loader
common/                boot/version shared ABI
kernel/arch/           x86-64 architecture code
kernel/drivers/        hardware drivers
kernel/memory/         PMM/VMM/kernel address spaces
kernel/task/           threads/processes/scheduler
kernel/user/           Ring-3 loader/syscall runtime
kernel/fs/             VFS/FAT32/RAMFS
kernel/storage/        AHCI/GPT/DMA/block devices
kernel/net/            network stack/drivers/services
kernel/install/        installer/package/live-root
kernel/ui/             window manager/UI rendering
sdk/                    public userspace ABI/libraries
userspace/              applications and PID1
```

## Ring-3 boundary

Every syscall handler must treat user input as hostile/untrusted.

Check:

- pointer belongs to the active process;
- complete range is mapped;
- write operations require writable mapping;
- sizes cannot overflow;
- structure size/version matches;
- flags contain no unknown bits;
- handles belong to the current process;
- no physical/kernel address is leaked.

## DMA

Never derive a hardware DMA address from an arbitrary C pointer.

Use the DMA allocator and its explicit physical address. Respect device address
width (for example DMA32 below 4 GiB when required).

## Drivers

A driver should expose a small subsystem API. Application code must not import
hardware headers.

Preferred structure:

```text
hardware driver
 -> kernel service/subsystem
 -> validated syscall
 -> SDK
 -> application
```

## Storage safety

Destructive disk operations require explicit authorization. The installer is
allowed to repartition only after the user chooses a device and enters the
confirmation token.

Do not add automatic formatting as a recovery shortcut.

## Build warnings

Kernel/SDK builds intentionally use strong warning flags. Do not silence a
warning globally just to land a patch. Fix the ownership/type/lifetime problem.

## Tests

For new functionality add at least one of:

- host unit test;
- image validation test;
- serial runtime marker;
- QEMU smoke test;
- VirtualBox smoke test for VirtualBox-specific media/hardware changes.

Security-sensitive changes should have deterministic failure tests too.

## Versioning public ABI

Do not reorder stable syscall numbers casually. Prefer appending a new number.
Use fixed-width public structures and static size assertions where possible.

## GUI/desktop

The normal desktop is userspace-owned. Do not reintroduce anonymous kernel
spawning of regular GUI apps as a convenience.

## DirectX/graphics

Do not add fake D3D entry points that return success while doing nothing.
Graphics compatibility must report capabilities truthfully.

See [`../GRAPHICS_COMPATIBILITY.md`](../GRAPHICS_COMPATIBILITY.md).

## Commit scope

Keep unrelated subsystems out of the same logical fix when possible. For a
large milestone, document why multiple subsystems must change together.

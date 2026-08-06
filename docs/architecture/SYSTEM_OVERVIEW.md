# System overview

EDK2 loads `EFI/BOOT/BOOTX64.EFI`. The standalone loader reads the PIE
`kernel.elf`, validates and maps its load segments, applies
`R_X86_64_RELATIVE` relocations, obtains GOP and the UEFI memory map, exits boot
services, and enters the assembly entry point using `KuroganeBootInfo` v1.

The kernel configures the framebuffer terminal, static kernel heap, physical
frame bitmap, RAMFS, PCI enumeration, loopback network service, cooperative
scheduler, IDT, remapped legacy PIC, PIT, and PS/2 keyboard. A shell dispatches
kernel-resident applications using an immediate-mode framebuffer UI.

There is currently no virtual-memory manager beyond firmware mappings, GDT/TSS
setup owned by the kernel, ring-3 runtime, syscall ABI, process isolation, IPC,
block layer, VFS, storage driver, compositor, service manager, or package
format. Those boundaries must be introduced before calling this a desktop OS.

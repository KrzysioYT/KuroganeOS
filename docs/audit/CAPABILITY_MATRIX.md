# Capability matrix

| Capability | Status | Evidence |
|---|---|---|
| UEFI x86-64 loader and boot protocol | tested in QEMU | staged and FAT32 boots |
| PIE freestanding kernel | implemented | ELF validation in `build.ps1` |
| Physical allocator and kernel heap | tested in QEMU | boot self-test; hosted tests |
| IDT, exceptions, PIC, PIT | tested in QEMU | timer/interrupt readiness |
| PS/2 keyboard | tested in QEMU | injected keyboard system test |
| Cooperative task scheduler | implemented | hosted unit test |
| Public application ABI | experimental foundation | versioned descriptor, status codes, generated headers and layout test |
| Processes, threads, user mode, syscalls | missing | descriptor reports transport unavailable; no privilege transition |
| IPC | missing | no implementation |
| RAMFS | tested in QEMU | shell read and hosted unit test |
| VFS and persistent filesystem | missing | RAMFS only |
| AHCI/NVMe/GPT | missing | PCI enumeration only |
| Framebuffer drawing and simple GUI apps | tested in QEMU | GUI launch/exit |
| Compositor/window manager | missing | immediate-mode single app |
| Network protocol helpers/loopback | tested in QEMU | hosted and shell tests |
| NIC, DHCP, DNS, TCP | missing | no hardware network driver |
| Audio | missing | no subsystem |
| Installer and first boot | missing | no target-side implementation |
| Package/update/recovery | missing | no implementation |
| UEFI live ISO | implemented | El Torito FAT32 image |
| Installed-system persistence | blocked | storage stack is missing |

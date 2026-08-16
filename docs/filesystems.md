# Filesystems and storage

## Stack

```text
application -> read-only public file ABI -> root_volume -> VFS
                                                    |-- boot RAMFS
                                                    `-- FAT32
                                                         -> PartitionDevice
                                                         -> GPT -> AHCI disk
```

The VFS normalizes absolute paths, resolves mount prefixes, tracks per-context
root/current directories and exposes stat, open/read/write/seek, directory
iteration and bounded mutations to kernel callers. It has 16 mount slots and
64 generation-checked open-file slots. Userspace 2.0 deliberately exposes only
open-for-read, read and close; writable VFS operations remain kernel/installer
APIs until a stable permission model exists.

## RAMFS

The hierarchical RAMFS is initialized during boot and retains compatibility
with early boot content and safe mode. It supports directories, files and the
same VFS routing contract but is volatile. If no valid persistent root can be
mounted, the kernel keeps RAMFS rather than inventing a successful disk mount.

## Block devices, GPT and AHCI

The central device/driver model discovers PCI AHCI controllers and registers
512-byte logical SATA block devices. AHCI uses DMA command structures and
implements checked read, write and cache flush. GPT parsing validates header
signature/revision/size, both CRC domains, LBA arithmetic, entry limits,
partition bounds and overlap before creating a PartitionDevice.

The reference 512 MiB installed/base image has:

| Partition | Start | Size/use |
|---|---:|---|
| protective MBR + primary GPT | LBA 0 | metadata |
| EFI System Partition | LBA 2048 | 64 MiB FAT32 (`KURO_ESP`) |
| root | LBA 133120 | remaining FAT32 (`KURO_ROOT`) |
| backup GPT | disk tail | metadata |

## Writable FAT32

The driver validates BPB geometry, FAT bounds, cluster chains and directory
records before use. It supports reading, create/write/extend, unlink,
same-directory rename, mkdir, empty rmdir and sync. Cluster allocation updates
both FAT copies; truncation/deletion releases chains; FSInfo supplies a bounded
allocation hint. Device flush completes persistence.

The mutating path is intentionally limited to ASCII FAT 8.3 names. Read-side
directory parsing can represent longer names, but the installer package and
system paths stay within the short-name contract. VFS paths are at most 255
bytes and 32 components.

## Mounts and system paths

Normal QEMU boot discovers GPT and mounts the root partition at `/`. Important
paths are `/etc/system.cfg`, `/system/init`, `/apps/*`, `/gui/*` and `/var`.
The loader/kernel remains on the ESP as `EFI/BOOT/BOOTX64.EFI` and `kernel.elf`;
the installer also writes `/boot/kernel.elf` on root.

## Persistence and installer proof

The storage system test writes a tagged `/var/PERSIST.DAT`, syncs and boots the
same purpose-built QEMU image twice. The installer test creates a blank file
under `build/test-disks`, writes protective MBR + primary/backup GPT, formats
both partitions, copies and reads back every package file, then boots twice
without ISO. No physical disk path is auto-discovered or attached.

## Limitations

There is no journaling, permissions/UIDs, symlinks, hard links, timestamps API,
file locking, mmap, partitions beyond GPT, NVMe, USB mass storage or crash
recovery guarantee. A power loss during FAT metadata mutation can corrupt the
volume; keep test images disposable and backed up.

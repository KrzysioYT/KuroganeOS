KuroganeOS Foundation root filesystem.

The kernel mounts this FAT32 volume read-only through PartitionDevice and VFS
and validates /etc/system.conf during boot. The interactive kernel shell still
uses RAMFS. Persistent writes are not a supported runtime feature yet.

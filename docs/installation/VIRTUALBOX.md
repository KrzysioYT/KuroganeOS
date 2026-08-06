# VirtualBox

Expected, but untested, live-boot configuration: x86-64 guest, EFI enabled,
256 MiB or more RAM, at least 2 vCPU, SATA AHCI, an empty virtual disk, and
`kurogane.iso` in the optical drive. Networking should be disabled because no
VirtualBox-compatible NIC driver exists.

Installation to the virtual disk is not implemented. Secure Boot must be off.

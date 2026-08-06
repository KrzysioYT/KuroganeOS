# QEMU

Use the repository scripts for the verified FAT32 boot profile. To inspect the
live ISO manually:

```bash
qemu-system-x86_64 -machine q35 -m 256M \
  -drive if=pflash,format=raw,readonly=on,file=OVMF_CODE.fd \
  -cdrom kurogane.iso
```

There is no supported virtual-disk installation workflow yet.

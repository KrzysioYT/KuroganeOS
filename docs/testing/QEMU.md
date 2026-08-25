# QEMU quick reference

Pełna instrukcja: [`../QEMU_TESTING.md`](../QEMU_TESTING.md).

Bieżący pełny userspace wymaga Foundation GPT image:

```text
build/images/KuroganeOS-base.img
```

Windows focused integration:

```powershell
.\scripts\run-qemu.ps1 `
  -UseDiskImage `
  -DiskImagePath .\build\images\KuroganeOS-base.img `
  -ShellTest `
  -TimeoutSeconds 90 `
  -MemoryMiB 1024 `
  -LogName qemu-foundation
```

Windows interactive GUI/performance:

```powershell
.\scripts\run-qemu-fast.ps1 `
  -Accelerator auto `
  -MemoryMiB 1024
```

WSL convenience:

```bash
./scripts/run-qemu.sh interactive
./scripts/run-qemu.sh system
./scripts/run-qemu.sh safe
```

`kurogane.img` jest legacy FAT/EFI artifactem; nie używaj go do normalnego
PID1/login/desktop testu.

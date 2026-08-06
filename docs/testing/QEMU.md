# QEMU testing

The verified profile uses `q35`, EDK2 pflash, `-cpu max`, 256 MiB RAM, a serial
file, no network device, and no display for automation. Run:

```bash
./scripts/run-qemu.sh smoke
./scripts/run-qemu.sh system
```

PowerShell users may call `scripts/run-qemu.ps1` directly. Use a unique
`-LogName` and monitor port for concurrent runs.

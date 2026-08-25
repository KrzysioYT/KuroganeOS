# Build status

Data: 25 sierpnia 2026 r.

## Current stage

Publiczna wersja pozostaje:

```text
3.3.3-dev
DEV BETA
```

Gałąź `gpt/kuroganeos-5-gui` rozwija warstwę **Forged Steel / KuroganeOS 5**.
Nie podnosimy numeru do 5.0.0 przed pełnym Definition of Done, release buildem i
zieloną kwalifikacją QEMU/VirtualBox.

## Working foundation

Aktualna gałąź zawiera m.in.:

- UEFI `BOOTX64.EFI` i boot protocol;
- VMM, GDT/TSS/IST, IDT;
- Ring-3 ELF64, syscall ABI, PID/TID;
- process spawn/wait/exit i PIT preemption;
- `/system/init` jako stabilny PID 1;
- AHCI/GPT/FAT32/VFS + persistent `Kurogane Root`;
- E1000 + DHCP/DNS/TCP/HTTP/HTTPS transport;
- Intel AC'97;
- WindowManager z session ownership;
- native UI ABI v2, icons i cursor hints;
- Forged Steel renderer;
- Blade Launcher, Kurosh, Vault, Anvil, Forge Control, Pulse, Web,
  Performance, System Monitor i About;
- software backbuffer/GOP compositor;
- Windows/WSL, macOS i Linux build/media tooling.

## Ostatni potwierdzony lokalnie stan

Podczas bieżącej pracy lokalny **debug build** przeszedł po usunięciu kolejnych
blockerów kompilacji, FAT 8.3 i Foundation validation. Wcześniejsze focused QEMU
runy dotarły do PID1 i graficznego secure-access loginu.

Pełny `verify.ps1` nie jest jeszcze oznaczony jako finalnie zielony dla aktualnej
gałęzi. W trakcie prac znaleziono i poprawiono m.in.:

```text
Electron-like CRLF -> WSL Bash payload issues
WindowManager host-test include path
Foundation boot-mode assertion
legacy FAT vs Foundation PID1 test mismatch
PID1 slot reservation
login role title regression
ShellTest waiting for an obsolete text prompt
```

Nie należy na tej podstawie publikować 5.0.0 ani deklarować release PASS.

## Bieżący build contract

Development:

```powershell
.\scripts\build.ps1 -Configuration debug -Rebuild
```

Najważniejsze artefakty:

```text
build/kernel.elf
build/BOOTX64.EFI
build/build-info.txt
build/images/KuroganeOS-base.img
state/KuroganeOS.img          # working image, jeżeli istnieje
kurogane.img                   # legacy FAT/EFI artifact
build/install.pkg
```

Windows media:

```powershell
.\scripts\build-media.ps1 -Configuration release -Rebuild
```

Publikowane nazwy Windows:

```text
dist/KuroganeOS-3.3.3-dev-qemu-x86_64.img
dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
dist/SHA256SUMS.txt
```

## QEMU qualification

Pełny userspace testuje Foundation GPT, nie legacy `kurogane.img`:

```powershell
.\scripts\run-qemu.ps1 `
  -UseDiskImage `
  -DiskImagePath .\build\images\KuroganeOS-base.img `
  -ShellTest `
  -TimeoutSeconds 90 `
  -MemoryMiB 1024 `
  -LogName focused
```

Publiczny `run-qemu.ps1` korzysta obecnie z graphical-aware core runnera.
`ShellTest` zachowuje historyczną nazwę, ale potrafi rozpoznać secure-access
login i Blade session zamiast bezwarunkowo czekać na `kurogane:user$`.

## GUI performance work

Aktywnie poprawiane są:

- coalescing mouse movement;
- ograniczenie redraw po każdym input evencie;
- wcześniejsze dostarczanie desktop input;
- ukryte redrawy Blade po app spawn;
- WHPX interactive development runner;
- dalsze ograniczenie kosztu software compositor/GOP scanout.

Do ręcznych pomiarów na Windows preferowany jest:

```powershell
.\scripts\run-qemu-fast.ps1 -Accelerator auto -MemoryMiB 1024
```

TCG pozostaje deterministycznym/fallback backendem i może znacząco zaniżać FPS.

## Full verifier

```powershell
.\scripts\verify.ps1 -TimeoutSeconds 90 -KeepLogs
```

Wymagany zakres obejmuje:

1. WSL/toolchain preflight;
2. clean debug build;
3. host tests;
4. FAT validation;
5. Foundation GPT/FAT validation;
6. Foundation QEMU integration;
7. Safe Mode;
8. test profile + AHCI scratch;
9. release build;
10. release ISO;
11. QEMU ISO qualification;
12. odpowiednią kwalifikację VirtualBox dla release media.

## Release blockers przed 5.0.0

- pełny verifier aktualnej rewizji musi być zielony;
- app launch/focus/dock musi przejść runtime test bez regresji;
- compositor latency/FPS wymaga dalszej optymalizacji;
- login/Blade/Vault/Forge/Pulse/Anvil wymagają kolejnych visual passes względem
  `Forged_Steel_GUI_Reference.png`;
- scalable font stack nie jest jeszcze gotowy;
- hardware accelerated compositor/D3D nie jest produkcyjnie gotowy;
- Web nie jest Chromium;
- Anvil package authenticity/signing nie jest jeszcze finalnym rozwiązaniem;
- real VirtualBox/install/reboot path musi przejść kwalifikację release.

## Zobacz także

- [RUNNING.md](RUNNING.md)
- [BUILDING.md](BUILDING.md)
- [TESTING.md](TESTING.md)
- [QEMU_TESTING.md](QEMU_TESTING.md)
- [CURRENT_LIMITATIONS.md](CURRENT_LIMITATIONS.md)
- [roadmap/KUROGANEOS_5_GUI.md](roadmap/KUROGANEOS_5_GUI.md)

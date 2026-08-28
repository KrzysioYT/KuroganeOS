# Build status

Data: 28 sierpnia 2026 r.

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

## Ostatni potwierdzony stan

Aktualny pion systemu przeszedł lokalne host tests, pełny zestaw `scripts/test.sh`
oraz kompilację Linux release. Oficjalny workflow GitHub zbudował ISO i IMG,
wykonał 20 kontroli struktury ISO, uruchomił obraz przez OVMF i zakończył zielono
osobne kwalifikacje E1000, PCnet oraz VirtIO-net.

Runtime potwierdził m.in. start PID 1, aplikacje Ring-3, VFS, pulpit Forged Steel,
DHCP, DNS, TCP i HTTP. Oficjalny bundle Mozilla CA jest ładowany z obrazu; build
nie importuje magazynu zaufania hosta.

Publiczne `docs.kuroganeos.dev` i `repo.kuroganeos.dev` nie mogą jeszcze przejść
końcowego testu DNS/HTTPS, dopóki rekordy domeny nie wskazują na publikowany
portal. Brak DNS jest raportowany fail-closed i nie jest zastępowany wynikiem
pozorowanym.

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

## GUI i aplikacje

Aktualna implementacja obejmuje:

- stały panel Blade z Home, Vault, System, Terminal, Docs i Anvil;
- pełny topbar i dolny Pulse Ribbon z aktywnymi oknami;
- focus, minimalizację, maksymalizację, move/resize i przywracanie z ribbonu;
- automatyczne utrzymanie aktywnego widgetu w widocznym obszarze;
- natywny Kurogane Web z historią, redirectami, ekstrakcją tekstu i linków;
- ograniczenie redraw po wejściu i coalescing ruchu myszy;
- WHPX interactive development runner;
- programowy compositor/GOP scanout.

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

- publiczny DNS i końcowy HTTPS dla docs/repo/downloads muszą być zielone;
- compositor latency/FPS wymaga dalszej optymalizacji;
- login/Blade/Vault/Forge/Pulse/Anvil wymagają kolejnych visual passes względem
  `Forged_Steel_GUI_Reference.png`;
- scalable font stack nie jest jeszcze gotowy;
- hardware accelerated compositor/D3D nie jest produkcyjnie gotowy;
- Web nie jest Chromium;
- Anvil package authenticity/signing nie jest jeszcze finalnym rozwiązaniem;
- real VirtualBox/install/reboot path oraz szerszy sprzęt fizyczny muszą przejść
  kwalifikację release.

## Zobacz także

- [RUNNING.md](RUNNING.md)
- [BUILDING.md](BUILDING.md)
- [TESTING.md](TESTING.md)
- [QEMU_TESTING.md](QEMU_TESTING.md)
- [CURRENT_LIMITATIONS.md](CURRENT_LIMITATIONS.md)
- [roadmap/KUROGANEOS_5_GUI.md](roadmap/KUROGANEOS_5_GUI.md)

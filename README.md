# KuroganeOS 3.3.3-dev — DEV BETA

KuroganeOS to eksperymentalny 64-bitowy system operacyjny rozwijany od zera dla
**x86-64 + UEFI**. Nie jest dystrybucją Linuxa i nie używa kernela Linux.

> [!IMPORTANT]
> Publiczny numer development builda nadal wynosi **3.3.3-dev**. Bieżąca gałąź
> rozwija desktop **Forged Steel / KuroganeOS 5**, ale nie oznaczamy projektu
> jako 5.0.0 przed pełnym Definition of Done i kwalifikacją release.

Pierwszy raz? Zacznij od **[docs/START_HERE.md](docs/START_HERE.md)**.

## Aktualna architektura

```text
UEFI
 -> BOOTX64.EFI
 -> x86-64 kernel
 -> scheduler / VFS / storage / network / input
 -> PID 1: /system/init
 -> KUROGANE // SECURE ACCESS
 -> Blade Launcher session root
 -> Forged Steel desktop
 -> Ring-3 applications
```

Pełny userspace korzysta z Foundation GPT image zawierającego ESP oraz
`Kurogane Root`.

```text
build/images/KuroganeOS-base.img   # deterministic Foundation image
state/KuroganeOS.img               # persistent development working image
kurogane.img                        # legacy FAT/EFI artifact, nie pełny userspace disk
```

## Najszybsze uruchomienie na Windows

Po buildzie:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\run-qemu-fast.ps1 `
  -Accelerator auto `
  -MemoryMiB 1024 `
  -LogName forged-dev
```

Runner wybiera working image, a gdy go nie ma — Foundation base. Na Windows
najpierw próbuje WHPX, a następnie TCG.

```text
[active] accelerator=whpx   # preferowane do pracy nad GUI
[active] accelerator=tcg    # software emulation; znacznie wolniejsze
```

Pełna instrukcja: **[docs/RUNNING.md](docs/RUNNING.md)**.

## Build Windows + WSL2

Wymagany jest repozytoryjny cross-toolchain:

```text
tools/compiler/x86_64-elf/bin/
```

Development build:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\build.ps1 `
  -Configuration debug `
  -Rebuild
```

Media:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\build-media.ps1 `
  -Configuration release `
  -Rebuild
```

Bieżący kontrakt Windows media:

```text
dist/KuroganeOS-3.3.3-dev-qemu-x86_64.img
dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
dist/SHA256SUMS.txt
```

Stare aliasy `*-windows-qemu.img` i ogólne `*-x86_64.iso` nie są kanonicznym
kontraktem Windows.

## macOS

```bash
chmod +x scripts/*.sh
bash ./scripts/setup-macos.sh --install
bash ./scripts/build-media-macos.sh --configuration debug --rebuild
./scripts/run-qemu-macos.sh --windowed --memory 1024
```

Apple Silicon uruchamia x86-64 KuroganeOS przez QEMU TCG. Taki run jest dobry do
funkcjonalnych testów, ale nie do oceny FPS software compositora.

Więcej: [docs/MACOS_DEVELOPMENT.md](docs/MACOS_DEVELOPMENT.md).

## VirtualBox

Użyj:

```text
dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
```

Referencyjnie:

```text
EFI/UEFI:       ON
Secure Boot:    OFF
RAM:            1024-2048 MiB
CPU:            1-2
System disk:    SATA / Intel AHCI
DVD:            KuroganeOS ISO
Network:        NAT
NIC:            Intel PRO/1000 MT Desktop (82540EM)
Audio:          Intel AC'97
Input:          PS/2
```

Więcej: [docs/VIRTUALBOX.md](docs/VIRTUALBOX.md).

## Forged Steel desktop

Aktualny zestaw aplikacji:

- **Blade Launcher** — root graficznej sesji i launcher;
- **Kurosh** — terminal/developer shell;
- **Vault** — VFS file manager;
- **Anvil** — package manager foundation;
- **Forge Control** — ustawienia systemu;
- **Pulse** — szybki status/system cards;
- **Kurogane Web** — natywna przeglądarka;
- **Performance** — live system metrics;
- **System Monitor** — runtime/process health;
- **About**.

WindowManager obsługuje focus/z-order, drag, resize, minimize, maximize,
restore, close, dock, shortcuts, software cursor i Ring-3 native UI ABI v2.

Kolory Forged Steel:

```text
Obsidian      #090E0E
Forged Steel  #171C22
Ash           #A8AFB8
Crimson       #E62932
Hot Edge      #FF4A45
```

**BUILT IN STEEL. REFINED IN FIRE.**

## Kurogane Web

KuroganeOS ma własny E1000 + IPv4 stack z DHCP/DNS/TCP. Bieżący Web ma transport
HTTP i **HTTPS/TLS**, trust store, redirecty, Back/Home/Reload, historię,
aktywację linków oraz prosty natywny parser/rendering HTML/CSS.

To nadal **nie jest Chromium**. Pełny port Chromium wymaga znacznie szerszego
POSIX/libc/process/thread/socket/filesystem/sandbox stacku.

## Anvil

Anvil korzysta z zewnętrznego repozytorium pakietów. Konfiguracja rootfs jest
FAT 8.3-safe:

```text
/etc/anvil.cfg
```

Nie używaj starego `/etc/anvil.repo`.

## Testy

Host tests:

```powershell
wsl.exe --exec bash -lc "cd /mnt/e/KuroganeOS && bash ./scripts/test.sh"
```

Focused Foundation integration:

```powershell
.\scripts\run-qemu.ps1 `
  -UseDiskImage `
  -DiskImagePath .\build\images\KuroganeOS-base.img `
  -ShellTest `
  -TimeoutSeconds 90 `
  -LogName focused
```

Pełna kwalifikacja:

```powershell
.\scripts\verify.ps1 -TimeoutSeconds 90 -KeepLogs
```

`ShellTest` zachowuje historyczną nazwę parametru, ale aktualny runner rozumie
zarówno graficzny Foundation login/session, jak i Safe Mode console.

Więcej: [docs/TESTING.md](docs/TESTING.md) i
[docs/QEMU_TESTING.md](docs/QEMU_TESTING.md).

## Status wydajności GUI

Forged Steel używa obecnie software compositora nad UEFI GOP. Trwa optymalizacja
input latency, damage/redraw i scanoutu. Do ręcznych testów na Windows preferuj
WHPX przez `run-qemu-fast.ps1`; TCG może znacząco zaniżać FPS.

Hardware accelerated 3D nie jest jeszcze backendem produkcyjnym. D3D9/11/12
compatibility work nie jest równoznaczne z pełną zgodnością DirectX.

## Dokumentacja

### Użytkownik / uruchamianie

- [START_HERE.md](docs/START_HERE.md)
- [RUNNING.md](docs/RUNNING.md)
- [BUILDING.md](docs/BUILDING.md)
- [QEMU_TESTING.md](docs/QEMU_TESTING.md)
- [TESTING.md](docs/TESTING.md)
- [VIRTUALBOX.md](docs/VIRTUALBOX.md)
- [MACOS_DEVELOPMENT.md](docs/MACOS_DEVELOPMENT.md)

### Development

- [DEVELOPERS/README.md](docs/DEVELOPERS/README.md)
- [DEVELOPERS/APP_DEVELOPMENT.md](docs/DEVELOPERS/APP_DEVELOPMENT.md)
- [DEVELOPERS/GUI_APPLICATIONS.md](docs/DEVELOPERS/GUI_APPLICATIONS.md)
- [DEVELOPERS/API_REFERENCE.md](docs/DEVELOPERS/API_REFERENCE.md)
- [DEVELOPERS/KERNEL_CONTRIBUTION.md](docs/DEVELOPERS/KERNEL_CONTRIBUTION.md)

### System

- [ARCHITECTURE.md](docs/ARCHITECTURE.md)
- [BOOT_PROCESS.md](docs/BOOT_PROCESS.md)
- [GUI.md](docs/GUI.md)
- [FILESYSTEM.md](docs/FILESYSTEM.md)
- [NETWORKING.md](docs/NETWORKING.md)
- [GRAPHICS_COMPATIBILITY.md](docs/GRAPHICS_COMPATIBILITY.md)
- [CURRENT_LIMITATIONS.md](docs/CURRENT_LIMITATIONS.md)

## Licencja

Bieżące rewizje są udostępniane zgodnie z **KuroganeOS Source-Available License
2.0 (KSAL-2.0)**. Szczegóły: [LICENSE](LICENSE), [docs/LICENSING.md](docs/LICENSING.md),
[CLA.md](CLA.md) i [CONTRIBUTING.md](CONTRIBUTING.md).

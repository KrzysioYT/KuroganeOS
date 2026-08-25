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

Pełny userspace korzysta z Foundation GPT image:

```text
build/images/KuroganeOS-base.img   # deterministic Foundation image
state/KuroganeOS.img               # persistent development working image
kurogane.img                        # legacy FAT/EFI artifact
```

`kurogane.img` nie jest pełnym system disk — nie zawiera `Kurogane Root`.

## Najszybsze uruchomienie na Windows

Po buildzie:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\run-qemu-fast.ps1 `
  -Accelerator auto `
  -MemoryMiB 1024 `
  -LogName forged-dev
```

Runner wybiera working image, a gdy go nie ma — Foundation base.

```text
[active] accelerator=whpx   # preferowane do pracy nad GUI
[active] accelerator=tcg    # software emulation; znacznie wolniejsze
```

Pełna instrukcja: [docs/RUNNING.md](docs/RUNNING.md).

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

Windows media contract:

```text
dist/KuroganeOS-3.3.3-dev-qemu-x86_64.img
dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
dist/SHA256SUMS.txt
```

## QEMU

Referencyjny development hardware:

```text
Machine: q35
Firmware: EDK2/UEFI
NIC: Intel E1000 + QEMU user NAT
RAM: 1024 MiB
System image: Foundation GPT / working image
```

Do deterministycznych testów używaj `scripts/run-qemu.ps1`. Do ręcznej pracy
nad GUI na Windows preferuj `scripts/run-qemu-fast.ps1` + WHPX.

Bieżący `ShellTest` potrafi zweryfikować:

```text
PID1
Secure Access
Blade Launcher
real Kurosh child-app launch
storage/network/input markers
```

Nie czeka już bezwarunkowo na stary tekstowy prompt.

## VirtualBox

Użyj:

```text
dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
```

Canonical Oracle VirtualBox profile:

```text
EFI/UEFI:       ON
Secure Boot:    OFF
RAM:            1024-2048 MiB
CPU:            1-2
Graphics:       VMSVGA / 128 MiB / 3D OFF
System disk:    SATA / Intel AHCI
DVD:            KuroganeOS ISO
Network:        NAT
NIC:            PCnet-FAST III (Am79C973)
Audio:          Intel AC'97
Input:          PS/2
```

QEMU i VirtualBox celowo mają obecnie inne referencyjne NIC-i. E1000 i VirtIO
są również testowane jako profile dodatkowe.

Więcej: [docs/VIRTUALBOX.md](docs/VIRTUALBOX.md).

## macOS

```bash
chmod +x scripts/*.sh
bash ./scripts/setup-macos.sh --install
bash ./scripts/build-media-macos.sh --configuration debug --rebuild
./scripts/run-qemu-macos.sh --windowed --memory 1024
```

Apple Silicon uruchamia x86-64 KuroganeOS przez QEMU TCG. To dobre środowisko
do testów funkcjonalnych, ale nie do oceny FPS software compositora.

Więcej: [docs/MACOS_DEVELOPMENT.md](docs/MACOS_DEVELOPMENT.md).

## Forged Steel desktop

Aktualne aplikacje:

- **Blade Launcher** — root graficznej sesji;
- **Kurosh** — terminal/developer shell;
- **Vault** — VFS file manager;
- **Anvil** — package manager;
- **Forge Control** — ustawienia;
- **Pulse** — quick status/system cards;
- **Kurogane Web** — natywna przeglądarka;
- **Performance** — live metrics;
- **System Monitor** — runtime/process health;
- **About**.

WindowManager obsługuje focus/z-order, drag, resize, minimize, maximize,
restore, close, dock, desktop shortcuts, software cursor i Ring-3 UI ABI v2.

Kolory Forged Steel:

```text
Obsidian      #090E0E
Forged Steel  #171C22
Ash           #A8AFB8
Crimson       #E62932
Hot Edge      #FF4A45
```

**BUILT IN STEEL. REFINED IN FIRE.**

## GUI performance

Forged Steel nadal używa software compositora nad UEFI GOP. Bieżący performance
work obejmuje:

- koaleskowanie ruchów myszy;
- ograniczenie redraw po każdym input evencie;
- wcześniejsze dostarczanie desktop input;
- eliminację ukrytych redrawów Blade po spawn;
- RAM front-shadow zamiast odczytywania GOP/VRAM piksel po pikselu przy każdym
  compositor frame;
- dalszy rozwój damage/dirty-region rendering.

WHPX jest preferowany do ręcznego GUI development na Windows. TCG może znacząco
zaniżać FPS.

## Kurogane Web

KuroganeOS ma własny E1000 + IPv4 stack z DHCP/DNS/TCP. Web ma transport HTTP i
**HTTPS/TLS**, trust store, redirecty, Back/Home/Reload, historię, link activation
i prosty natywny HTML/CSS rendering.

To nadal nie jest Chromium.

## Anvil

Konfiguracja package repository w rootfs używa FAT 8.3-safe ścieżki:

```text
/etc/anvil.cfg
```

Nie przywracaj starego `/etc/anvil.repo`.

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
  -MemoryMiB 1024 `
  -LogName focused
```

Pełna kwalifikacja:

```powershell
.\scripts\verify.ps1 -TimeoutSeconds 90 -KeepLogs
```

## Dokumentacja

- [START_HERE.md](docs/START_HERE.md)
- [RUNNING.md](docs/RUNNING.md)
- [BUILDING.md](docs/BUILDING.md)
- [QEMU_TESTING.md](docs/QEMU_TESTING.md)
- [TESTING.md](docs/TESTING.md)
- [VIRTUALBOX.md](docs/VIRTUALBOX.md)
- [MACOS_DEVELOPMENT.md](docs/MACOS_DEVELOPMENT.md)
- [LINUX_DEVELOPMENT.md](docs/LINUX_DEVELOPMENT.md)
- [GUI.md](docs/GUI.md)
- [CURRENT_LIMITATIONS.md](docs/CURRENT_LIMITATIONS.md)
- [BUILD_STATUS.md](docs/BUILD_STATUS.md)
- [DEVELOPERS/README.md](docs/DEVELOPERS/README.md)

## Licencja

Bieżące rewizje są udostępniane zgodnie z **KuroganeOS Source-Available License
2.0 (KSAL-2.0)**. Szczegóły: [LICENSE](LICENSE), [docs/LICENSING.md](docs/LICENSING.md),
[CLA.md](CLA.md) i [CONTRIBUTING.md](CONTRIBUTING.md).

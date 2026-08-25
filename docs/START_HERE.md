# KuroganeOS — START HERE

KuroganeOS `3.3.3-dev` jest eksperymentalnym systemem x86-64 rozwijanym od
zera. Bieżąca gałąź desktopowa rozwija UI Forged Steel/KuroganeOS 5, ale nie
jest jeszcze wydaniem 5.0.0.

Najbezpieczniej testować system w QEMU albo VirtualBox. Nie instaluj DEV BETA na
dysku zawierającym ważne dane.

## Chcę tylko zobaczyć bieżący desktop na Windows

Po buildzie:

```powershell
.\scripts\run-qemu-fast.ps1 `
  -Accelerator auto `
  -MemoryMiB 1024 `
  -LogName first-run
```

Runner wybiera `state/KuroganeOS.img`, jeżeli istnieje, w przeciwnym razie
`build/images/KuroganeOS-base.img`.

Na ekranie powinieneś zobaczyć:

```text
Forged Steel splash
KUROGANE // SECURE ACCESS
ENTER KUROGANE DESKTOP
Blade Launcher / desktop
```

Jeżeli terminal wypisze `accelerator=whpx`, QEMU używa Windows Hypervisor
Platform. Przy `accelerator=tcg` system działa programowo i GUI może być dużo
wolniejsze.

## Foundation a legacy FAT

Do pełnego desktopu używaj:

```text
build/images/KuroganeOS-base.img
state/KuroganeOS.img
```

Nie używaj `kurogane.img` jako normalnego dysku userspace. To legacy 64 MiB
FAT/EFI artifact bez partycji `Kurogane Root` wymaganej przez `/system/init`.

## Chcę uruchomić media QEMU

Po:

```powershell
.\scripts\build-media.ps1 -Configuration release -Rebuild
```

Windows publikuje:

```text
dist/KuroganeOS-3.3.3-dev-qemu-x86_64.img
dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
dist/SHA256SUMS.txt
```

QEMU IMG:

```powershell
.\scripts\run-qemu-fast.ps1 `
  -DiskImagePath .\dist\KuroganeOS-3.3.3-dev-qemu-x86_64.img `
  -Accelerator auto
```

## Chcę uruchomić KuroganeOS w VirtualBox

Użyj:

```text
dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
```

Referencyjna konfiguracja:

```text
Firmware:       EFI / UEFI
Secure Boot:    OFF
I/O APIC:       ON
RAM:            1024-2048 MiB
CPU:            1-2
HDD:            pusty VDI >= 2 GiB
HDD controller: SATA / Intel AHCI
DVD controller: IDE
DVD:            KuroganeOS VirtualBox ISO
Boot order:     DVD -> Disk
Network:        NAT
NIC:            Intel PRO/1000 MT Desktop (82540EM)
Audio:          Intel AC'97
Input:          PS/2
```

Szczegóły: [VIRTUALBOX.md](VIRTUALBOX.md).

## Chcę zbudować system na Windows

Wymagane są repozytoryjne pliki cross-toolchainu:

```text
tools/compiler/x86_64-elf/bin/
```

oraz WSL2.

Development build:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\build.ps1 `
  -Configuration debug `
  -Rebuild
```

Pełne media:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\build-media.ps1 `
  -Configuration release `
  -Rebuild
```

## macOS

```bash
chmod +x scripts/*.sh
bash ./scripts/setup-macos.sh --install
bash ./scripts/build-media-macos.sh --configuration debug --rebuild
./scripts/run-qemu-macos.sh --windowed --memory 1024
```

Apple Silicon uruchamia x86-64 guest przez QEMU TCG. Nie traktuj FPS z TCG jako
miarodajnego benchmarku compositora.

## Testy przed zgłoszeniem błędu

Host tests:

```powershell
wsl.exe --exec bash -lc "cd /mnt/e/KuroganeOS && bash ./scripts/test.sh"
```

Focused QEMU integration:

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

## Kurogane Web

Bieżący Web używa własnego stosu KuroganeOS. Obsługuje HTTP i HTTPS/TLS,
redirecty, historię, Back/Home/Reload, aktywację linków oraz prosty natywny
rendering HTML/CSS. Nie jest Chromium.

## Gdzie szukać dalej

- uruchamianie: [RUNNING.md](RUNNING.md)
- build: [BUILDING.md](BUILDING.md)
- QEMU: [QEMU_TESTING.md](QEMU_TESTING.md)
- pełne testy: [TESTING.md](TESTING.md)
- VirtualBox: [VIRTUALBOX.md](VIRTUALBOX.md)
- macOS: [MACOS_DEVELOPMENT.md](MACOS_DEVELOPMENT.md)
- aplikacje: [DEVELOPERS/README.md](DEVELOPERS/README.md)

## Zgłaszanie błędu

Podaj:

```text
commit / branch
host OS
QEMU/VirtualBox version
użyty IMG/ISO
WHPX czy TCG
ostatnie 100-200 linii serial log
screenshot
kroki odtworzenia
```

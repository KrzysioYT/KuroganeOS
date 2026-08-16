# KuroganeOS 2.2.5

KuroganeOS jest edukacyjnym, 64-bitowym systemem operacyjnym rozwijanym od
podstaw dla x86-64 i UEFI. Nie używa kernela Linux. Wydanie **2.2.5** zachowuje
Desktop Developer Preview 2.2.0 i dodaje ważny patch build/release: naprawiony
Flux Terminal oraz **natywne budowanie instalowalnego `.iso` na macOS** bez
PowerShella i bez konwersji `.img -> .iso`.

## Kurogane Flux Desktop Developer Preview

2.2 rozwija własny język wizualny **Kurogane Flux** zamiast kopiować Windows,
macOS, GNOME, KDE czy inny desktop environment.

Flux używa:

- grafitowej przestrzeni roboczej bez klasycznej metafory tapety;
- bocznego `signal spine` i status nodes;
- asymetrycznych surfaces zamiast klasycznych paneli;
- pływającego `pulse ribbon` zamiast typowego taskbara/docka;
- segmentowych wskaźników;
- jade/violet/amber jako kolorów sygnałowych;
- istniejącej mechaniki focus, z-order, drag, minimize/maximize/restore i close.

Uruchomienie desktop preview wykorzystuje istniejący tryb desktop/`boot=desktop`.
Aplikacje `/gui/terminal`, `/gui/files`, `/gui/sysmon`, `/gui/settings` i
`/gui/about` działają jako procesy Ring 3.

Szczegóły: [`docs/releases/DESKTOP_RELEASE.md`](docs/releases/DESKTOP_RELEASE.md).

## Flux Console

Domyślny userspace shell obsługuje m.in.:

```text
help clear version uname pid whoami status history jobs
pwd cd cat read which
apps run open gui wait
hello external files monitor about
echo calc sleep yield true false exit
mem free tasks pci device driver diskinfo
```

Przykłady:

```text
run test              -> /apps/test
run /apps/test        -> dokładna ścieżka ELF
gui terminal          -> /gui/terminal
jobs                   -> śledzone procesy w tle
wait <pid>             -> oczekiwanie na dziecko
```

Opis: [`docs/USERSPACE.md`](docs/USERSPACE.md).

## Fundament systemu

- własny `BOOTX64.EFI` i boot protocol v3;
- GDT/TSS/IST, IDT i obsługa wyjątków;
- czteropoziomowe page tables i VMM;
- Ring 3, prywatne przestrzenie adresowe i syscall gate `int 0x80`;
- procesy ELF64, PID/TID, spawn/wait/exit;
- osobne stosy i preempcja PIT;
- `/system/init` jako PID 1;
- PS/2 keyboard + mouse i wspólna kolejka input;
- PCI, ACPI MADT, APIC discovery;
- SATA/AHCI read/write/flush;
- GPT i writable persistent FAT32/VFS;
- installer UEFI oraz boot z zainstalowanego HDD;
- WindowManager i aplikacje GUI Ring 3;
- SDK: `crt0`, `libc`, `libkurogane`, `libui`;
- build/test na Windows/WSL i natywny development na macOS.

## Budowanie — Windows

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Rebuild
```

Installer release:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-installer.ps1 -Configuration release
```

Wynik:

```text
dist/KuroganeOS-2.2.5-x86_64.iso
dist/SHA256SUMS.txt
```

## Budowanie — macOS

Pierwsze przygotowanie:

```bash
chmod +x scripts/*.sh
./scripts/setup-macos.sh --install
```

### Development IMG

```bash
./scripts/build-macos.sh --configuration debug --rebuild
```

Wynik:

```text
dist/KuroganeOS-2.2.5-macos-qemu.img
```

### Development IMG + instalowalne ISO

```bash
./scripts/build-macos.sh --configuration release --rebuild --iso
```

Wyniki:

```text
dist/KuroganeOS-2.2.5-macos-qemu.img
dist/KuroganeOS-2.2.5-x86_64.iso
dist/SHA256SUMS.txt
kurogane.iso
```

### Tylko instalowalne ISO

```bash
./scripts/build-installer-macos.sh --configuration release --rebuild
```

Nie ma etapu konwersji IMG -> ISO. Builder tworzy osobny FAT32 EFI System
Partition, `install.pkg`, staging installera oraz właściwe UEFI/El Torito ISO
przez `xorriso`.

### Stara komenda `build-iso.sh`

Na macOS od 2.2.5 również działa natywnie:

```bash
./scripts/build-iso.sh release
```

Darwin automatycznie przechodzi do macOS installer buildera zamiast szukać
PowerShella.

### QEMU

```bash
./scripts/run-qemu-macos.sh --display
```

Własna aplikacja:

```bash
./scripts/build-app-macos.sh app.c -o app --install
./scripts/build-macos.sh --configuration debug --stage-only
./scripts/run-qemu-macos.sh --display
```

Następnie w Flux Console:

```text
run app
```

Szczegóły: [`docs/MACOS_DEVELOPMENT.md`](docs/MACOS_DEVELOPMENT.md).

## Naprawa 2.2.5 — `version.h`

2.2.0 mogło zatrzymać build GUI na macOS na:

```text
userspace/gui/terminal/main.c: fatal error: version.h: No such file or directory
```

2.2.5 naprawia include Flux Terminala i dodatkowo dodaje `common/` do ścieżek
include SDK buildera macOS.

## QEMU — Windows

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-qemu.ps1
```

## VirtualBox

KuroganeOS używa UEFI x86-64 oraz SATA/AHCI. Windowsowy helper:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-virtualbox.ps1
```

Instrukcje:

- [`docs/INSTALLATION.md`](docs/INSTALLATION.md)
- [`docs/VIRTUALBOX_TESTING.md`](docs/VIRTUALBOX_TESTING.md)

## Tryby startu

- normal/console;
- Desktop Developer Preview;
- safe mode;
- diagnostics;
- installer mode.

Safe mode udostępnia awaryjny kernel developer console.

## Znane ograniczenia

KuroganeOS 2.2 nadal jest Developer Preview. Najważniejsze braki:

- brak pełnego compositora GPU i resize wszystkich surfaces;
- brak kompletnego userspace `stat/readdir` i writable VFS capability ABI;
- brak pełnego userspace socket API;
- brak audio, pełnego recovery oraz szerokiej kwalifikacji real hardware;
- publiczne ABI/SDK nadal może ewoluować;
- Apple Silicon uruchamia x86-64 KuroganeOS przez QEMU TCG.

Aktualny opis: [`docs/CURRENT_LIMITATIONS.md`](docs/CURRENT_LIMITATIONS.md).

## Dokumentacja

- [`docs/releases/2.2.5.md`](docs/releases/2.2.5.md)
- [`docs/releases/2.2.0.md`](docs/releases/2.2.0.md)
- [`docs/releases/DESKTOP_RELEASE.md`](docs/releases/DESKTOP_RELEASE.md)
- [`docs/USERSPACE.md`](docs/USERSPACE.md)
- [`docs/BUILD_STATUS.md`](docs/BUILD_STATUS.md)
- [`docs/MACOS_DEVELOPMENT.md`](docs/MACOS_DEVELOPMENT.md)
- [`docs/INSTALLATION.md`](docs/INSTALLATION.md)
- [`docs/VIRTUALBOX_TESTING.md`](docs/VIRTUALBOX_TESTING.md)
- [`CHANGELOG.md`](CHANGELOG.md)

## Licencja

Aktualne rewizje KuroganeOS są udostępniane na warunkach **KuroganeOS
Source-Available License 1.0 (KSAL-1.0)**. Jest to licencja source-available,
a nie licencja Open Source zatwierdzona przez OSI.

- [`LICENSE`](LICENSE)
- [`LICENSE-MIT-LEGACY`](LICENSE-MIT-LEGACY)
- [`docs/LICENSING.md`](docs/LICENSING.md)
- [`CLA.md`](CLA.md)
- [`CONTRIBUTING.md`](CONTRIBUTING.md)

# KuroganeOS 2.1.1

KuroganeOS jest edukacyjnym, 64-bitowym systemem operacyjnym rozwijanym od podstaw dla architektury x86-64 i UEFI. Nie korzysta z kernela Linux. Wydanie **2.1.1** zachowuje instalowalny fundament 2.1 i dodaje natywne środowisko developerskie **macOS** do budowania kernela, UEFI, userspace, własnych aplikacji i testów QEMU bez WSL/Windows PowerShell.

Referencyjnym środowiskiem systemowym pozostaje **QEMU + EDK2**. Windows korzysta z istniejącego backendu PowerShell/WSL, a macOS z nowego backendu Bash + Homebrew `x86_64-elf`.

## Najważniejsze elementy 2.1.1

- własny bootloader `BOOTX64.EFI` i boot protocol v3;
- GDT/TSS/IST, IDT i obsługa wyjątków x86-64;
- czteropoziomowe page tables i własny VMM;
- prywatne przestrzenie adresowe procesów użytkownika;
- Ring 3 oraz syscall gate `int 0x80`;
- osobne stosy wątków i preempcja timerem PIT;
- process spawn/wait/exit oraz uruchamianie ELF64;
- `/system/init` uruchamiany jako **PID 1**;
- PS/2 keyboard + mouse oraz kolejka input;
- PCI, ACPI MADT i wykrywanie APIC;
- sterownik SATA/AHCI z read/write/flush;
- GPT oraz trwały root FAT32 montowany read-write przez VFS;
- installer tworzący GPT, ESP, root FAT32 i kopiujący system;
- boot z zainstalowanego wirtualnego dysku po odłączeniu ISO;
- userspace shell i aplikacje Ring 3;
- eksperymentalny desktop i aplikacje GUI w userspace;
- **natywny build na macOS** z Homebrew cross-toolchainem;
- **budowanie własnych aplikacji C/C++ na Macu** przez publiczne SDK;
- **QEMU smoke test na macOS**, również na hostach Apple Silicon przez emulację x86-64;
- userspace `run <path>` do uruchamiania własnych programów, np. `run /apps/test`.

## Budowanie — Windows

Kanoniczny frontend Windows:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Rebuild
```

Installer release:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-installer.ps1 -Configuration release
```

Po buildzie aktualnej wersji:

```text
dist/KuroganeOS-2.1.1-x86_64.iso
dist/SHA256SUMS.txt
```

## Budowanie — macOS

Pierwsze przygotowanie środowiska:

```bash
chmod +x scripts/*.sh
./scripts/setup-macos.sh --install
```

Pełny build debug:

```bash
./scripts/build-macos.sh --configuration debug
```

Release:

```bash
./scripts/build-macos.sh --configuration release
```

Wyniki macOS development build:

```text
build/kernel.elf
build/BOOTX64.EFI
build/sdk/sysroot/
build/userspace/rootfs/
build/images/KuroganeOS-macos.img
dist/KuroganeOS-2.1.1-macos-qemu.img
```

Szczegóły: [`docs/MACOS_DEVELOPMENT.md`](docs/MACOS_DEVELOPMENT.md).

## Własne aplikacje na macOS

Przykład C:

```bash
./scripts/build-app-macos.sh ./moja-aplikacja.c -o moja-aplikacja --install
./scripts/build-macos.sh --configuration debug --stage-only
./scripts/run-qemu-macos.sh --display
```

W userspace KuroganeOS:

```text
run /apps/moja-aplikacja
```

`--install` przechowuje development build programu w ignorowanym przez Git `state/macos-apps/`, więc zwykły rebuild systemu nie usuwa aplikacji.

## QEMU — Windows

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-qemu.ps1
```

Repozytorium zawiera także scenariusze instalacyjne wykorzystujące pusty wirtualny dysk SATA/AHCI. Commitowane logi instalatora dokumentują boot nośnika, deployment oraz boot persistent systemu.

## QEMU — macOS

Automatyczny test:

```bash
./scripts/run-qemu-macos.sh
```

Z oknem graficznym:

```bash
./scripts/run-qemu-macos.sh --display
```

Runner korzysta z `qemu-system-x86_64`, firmware EDK2 z instalacji Homebrew, development obrazu GPT/FAT32 oraz E1000 user networking. Sukces jest zgłaszany dopiero po markerach `userspace_init_spawn: PASS` i `ALL_REQUIRED_TESTS_PASSED`.

## VirtualBox

KuroganeOS jest przygotowany do UEFI x86-64 i SATA/AHCI. Windowsowy helper:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-virtualbox.ps1
```

Szczegóły konfiguracji i ręcznego scenariusza instalacji:

- [`docs/INSTALLATION.md`](docs/INSTALLATION.md)
- [`docs/VIRTUALBOX_TESTING.md`](docs/VIRTUALBOX_TESTING.md)

## Tryby startu

Aktualny loader zachowuje:

- normal/console;
- desktop alpha;
- safe mode;
- diagnostics;
- installer mode dla nośnika instalacyjnego.

Safe mode ogranicza część inicjalizacji urządzeń i udostępnia awaryjny kernel shell.

## Stan storage i instalatora

```text
UEFI ISO / GPT development image
  -> BOOTX64.EFI
  -> kernel
  -> SATA/AHCI
  -> GPT
  -> EFI System Partition FAT32
  -> KuroganeOS root FAT32
  -> persistent root
  -> /system/init
  -> PID 1 / Ring 3
```

Instalator nie powinien być używany na dysku z ważnymi danymi. KuroganeOS pozostaje projektem eksperymentalnym.

## Znane ograniczenia

- recovery environment nie jest jeszcze pełnym środowiskiem naprawczym; safe mode i diagnostics są obecnymi mechanizmami awaryjnymi;
- real hardware UEFI nie jest jeszcze tak szeroko zweryfikowany jak QEMU;
- NVMe, audio i pełna obsługa nowoczesnego sprzętu nie są kompletne;
- desktop pozostaje eksperymentalny;
- publiczne ABI/SDK nadal ewoluuje i nie należy zakładać stabilności binarnej między wydaniami;
- macOS jest hostem developerskim; KuroganeOS nadal jest systemem x86-64, więc Apple Silicon używa emulacji x86-64 w QEMU;
- finalny macOS runtime PASS wymaga wykonania nowych skryptów na rzeczywistym Macu.

## Dokumentacja

- [`docs/MACOS_DEVELOPMENT.md`](docs/MACOS_DEVELOPMENT.md)
- [`docs/BUILD_STATUS.md`](docs/BUILD_STATUS.md)
- [`docs/INSTALLATION.md`](docs/INSTALLATION.md)
- [`docs/VIRTUALBOX_TESTING.md`](docs/VIRTUALBOX_TESTING.md)
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
- [`docs/RECOVERY.md`](docs/RECOVERY.md)
- [`CHANGELOG.md`](CHANGELOG.md)

## Licencja

Aktualne rewizje KuroganeOS są udostępniane na warunkach **KuroganeOS Source-Available License 1.0 (KSAL-1.0)**. Jest to licencja **source-available**, a nie licencja Open Source zatwierdzona przez OSI.

- [`LICENSE`](LICENSE) — bieżąca KSAL-1.0;
- [`LICENSE-MIT-LEGACY`](LICENSE-MIT-LEGACY) — historyczna licencja wcześniejszych rewizji;
- [`docs/LICENSING.md`](docs/LICENSING.md) — opis modelu licencjonowania;
- [`CLA.md`](CLA.md) i [`CONTRIBUTING.md`](CONTRIBUTING.md) — zasady contribution.

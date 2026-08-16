# KuroganeOS 2.1

KuroganeOS jest edukacyjnym, 64-bitowym systemem operacyjnym rozwijanym od podstaw dla architektury x86-64 i UEFI. Nie korzysta z kernela Linux. Wydanie **2.1** domyka fundamenty 2.0 i koncentruje się na praktycznym cyklu: boot UEFI → instalacja na SATA/AHCI → trwały FAT32 → boot z dysku → `/system/init` jako PID 1 w Ring 3.

Referencyjnym środowiskiem automatycznych testów pozostaje **QEMU + EDK2**. Repozytorium zawiera również helper do testów UEFI w **VirtualBox**.

## Najważniejsze elementy 2.1

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
- installer payload ładowany przez bootloader UEFI;
- instalator tworzący GPT, ESP, root FAT32 i kopiujący system;
- boot z zainstalowanego wirtualnego dysku po odłączeniu ISO;
- test trwałości danych między restartami;
- podstawowy userspace shell i aplikacje;
- eksperymentalny desktop i aplikacje GUI w userspace;
- fallback loopback oraz rozwijany stos sieciowy.

## Budowanie

Kanoniczny frontend na Windows:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Rebuild
```

Build korzysta z repozytoryjnego cross-toolchaina `x86_64-elf`, PowerShella, WSL oraz QEMU/EDK2 zgodnie z istniejącymi skryptami projektu.

### Instalacyjne ISO 2.1

Installer można zbudować bezpośrednio:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-installer.ps1 -Configuration release
```

Po poprawnym buildzie kanoniczny artefakt wydania znajduje się tutaj:

```text
dist/KuroganeOS-2.1-x86_64.iso
dist/SHA256SUMS.txt
```

Skrypt tworzy również lokalny `kurogane.iso` dla kompatybilności ze starszymi runnerami emulatorów. Wygenerowane obrazy nie są przeznaczone do commitowania do repozytorium.

## QEMU

Podstawowy test:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-qemu.ps1
```

Repozytorium zawiera także scenariusze instalacyjne, które wykorzystują pusty wirtualny dysk SATA/AHCI. Aktualne commitowane logi instalatora pokazują trzy kluczowe etapy:

- `build/logs/installer-first-boot-serial.log` — boot środowiska instalacyjnego i start PID 1 z pakietu boot;
- `build/logs/installer-deploy-serial.log` — detekcja dysku, GPT, format ESP/root FAT32, kopiowanie i weryfikacja systemu;
- `build/logs/installer-second-boot-serial.log` — boot z persistent root po instalacji i ponowny start `/system/init` jako PID 1.

Szczegóły testów znajdują się w `docs/TESTING.md` i `docs/BUILD_STATUS.md`.

## VirtualBox

KuroganeOS 2.1 jest przygotowany do UEFI x86-64 i SATA/AHCI. Helper testowy:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-virtualbox.ps1
```

Szczegóły konfiguracji i ręcznego scenariusza instalacji znajdują się w:

- [`docs/INSTALLATION.md`](docs/INSTALLATION.md)
- [`docs/VIRTUALBOX_TESTING.md`](docs/VIRTUALBOX_TESTING.md)

VirtualBox nie powinien być oznaczany jako automatycznie zweryfikowany, jeżeli `VBoxManage` nie był dostępny w danym środowisku testowym.

## Tryby startu

Aktualny loader zachowuje tryby projektu:

- normal/console;
- desktop alpha;
- safe mode;
- diagnostics;
- installer mode dla nośnika instalacyjnego.

Safe mode celowo ogranicza część inicjalizacji urządzeń i udostępnia awaryjny kernel shell.

## Stan storage i instalatora

Przepływ instalacyjny 2.1 wygląda następująco:

```text
UEFI ISO
  -> BOOTX64.EFI
  -> kernel + install.pkg
  -> SATA/AHCI target
  -> protective MBR + GPT
  -> EFI System Partition FAT32
  -> KuroganeOS root FAT32
  -> EFI/BOOT/BOOTX64.EFI + kernel + userspace
  -> verification + flush
  -> reboot
  -> UEFI boot z HDD
  -> persistent root
  -> /system/init
  -> PID 1 / Ring 3
```

Instalator nie powinien być używany na dysku z ważnymi danymi. KuroganeOS 2.1 pozostaje projektem eksperymentalnym.

## Znane ograniczenia

- recovery environment nie jest jeszcze pełnym środowiskiem naprawczym; safe mode i diagnostics są obecnymi mechanizmami awaryjnymi;
- real hardware UEFI nie jest jeszcze tak szeroko zweryfikowany jak QEMU;
- NVMe, audio i pełna obsługa nowoczesnego sprzętu nie są kompletne;
- desktop pozostaje eksperymentalny;
- publiczne ABI/SDK nadal ewoluuje i nie należy zakładać stabilności binarnej między wydaniami;
- nie wszystkie opcjonalne testy sieciowe są wymagane do uznania instalatora za działający.

## Dokumentacja

Najważniejsze dokumenty:

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

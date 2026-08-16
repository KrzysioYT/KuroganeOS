# KuroganeOS 2.2.0

KuroganeOS jest edukacyjnym, 64-bitowym systemem operacyjnym rozwijanym od
podstaw dla x86-64 i UEFI. Nie używa kernela Linux. Wydanie **2.2.0** zachowuje
instalowalny fundament 2.1 oraz natywny workflow macOS z 2.1.1, a następnie
domyka obiecany wcześniej **Desktop Developer Preview**.

2.2 skupia się na dwóch rzeczach, które po przejściu do Ring 3 pozostały w
stanie prototypowym: pulpicie i powłoce użytkownika.

## Kurogane Flux Desktop Developer Preview

2.2 wprowadza własny język wizualny **Kurogane Flux**. Celem nie jest kopiowanie
Windows, macOS, GNOME, KDE ani innego desktop environment.

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

## Flux Console 2.2

Domyślny userspace shell nie jest już małym testem z kilkoma komendami. 2.2
przywraca dużą część codziennego workflow przez istniejące ABI:

```text
help clear version uname pid whoami status history jobs
pwd cd cat read which
apps run open gui wait
hello external files monitor about
echo calc sleep yield true false exit
mem free tasks pci device driver diskinfo
```

Najważniejsze:

```text
run test              -> /apps/test
run /apps/test        -> dokładna ścieżka ELF
gui terminal          -> /gui/terminal
jobs                   -> śledzone procesy w tle
wait <pid>             -> oczekiwanie na dziecko
```

Stary kernel developer console nadal posiada bardziej uprzywilejowane komendy.
Jeżeli Ring-3 ABI nie ma jeszcze bezpiecznej capability dla danej operacji, Flux
Console raportuje to jawnie zamiast udawać implementację albo kończyć zwykłym
`command not found`.

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
dist/KuroganeOS-2.2.0-x86_64.iso
dist/SHA256SUMS.txt
```

## Budowanie — macOS

Pierwsze przygotowanie:

```bash
chmod +x scripts/*.sh
./scripts/setup-macos.sh --install
```

Pełny development build:

```bash
./scripts/build-macos.sh --configuration debug --rebuild
```

Release build:

```bash
./scripts/build-macos.sh --configuration release
```

Wyniki:

```text
build/kernel.elf
build/BOOTX64.EFI
build/sdk/sysroot/
build/userspace/rootfs/
build/images/KuroganeOS-macos.img
dist/KuroganeOS-2.2.0-macos-qemu.img
```

Test z GUI:

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

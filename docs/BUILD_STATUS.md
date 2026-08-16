# Build status

Data: 16 sierpnia 2026 r.

## Current stage

KuroganeOS **2.1.1** jest poprawką developerską do Installable System Release 2.1. System nadal obejmuje UEFI boot, procesy Ring 3, PID 1, SATA/AHCI, GPT, writable FAT32/VFS i instalator, a 2.1.1 dodaje równoległy, natywny backend developerski dla macOS.

## Working in current source

- profile `debug`, `release` i `test`;
- Windows: `scripts/build.ps1`, repozytoryjny toolchain `.exe`, WSL/QEMU;
- macOS: `scripts/build-macos.sh`, Homebrew `x86_64-elf-*`, natywne Bash/Python i QEMU;
- host-aware Makefile zachowujący oba backendy;
- własny UEFI `BOOTX64.EFI` i boot protocol v3;
- czteropoziomowe page tables, własny VMM, GDT/TSS/IST i IDT;
- Ring 3, `int 0x80`, prywatne przestrzenie adresowe i ELF64 userspace;
- process spawn/wait/exit, osobne stosy oraz timer preemption;
- `/system/init` jako PID 1 i userspace console;
- PCI, ACPI MADT, APIC discovery oraz fallback PIC;
- PS/2 keyboard, PS/2 mouse i wspólna kolejka input;
- SATA/AHCI read/write/flush;
- GPT read/write oraz protective MBR;
- writable FAT32 i persistent root przez VFS;
- SDK: `crt0`, `libc`, `libkurogane`, `libui`, external app i desktop apps;
- macOS helper do budowania własnych aplikacji C/C++ i trwały staging `state/macos-apps/`;
- userspace `run <path>` do uruchamiania własnych ELF-ów;
- macOS Foundation GPT/FAT32 image oraz QEMU smoke-test z E1000 i serial logiem;
- installer package z bootloaderem, kernelem i userspace rootfs;
- safe mode, diagnostics, QEMU helper Windows/macOS oraz VirtualBox helper Windows.

## Existing QEMU evidence for the 2.1 foundation

Commitowane logi z implementacji 2.1 dokumentują działający scenariusz instalacyjny:

- `build/logs/installer-first-boot-serial.log` — start środowiska i PID 1;
- `build/logs/installer-deploy-serial.log` — SATA target, GPT, FAT32, kopiowanie i weryfikacja;
- `build/logs/installer-second-boot-serial.log` — persistent root, `/system/init`, PID 1 i userspace console.

Te logi pozostają dowodem działania bazowego systemu 2.1, ale nie są dowodem wykonania nowego backendu macOS 2.1.1.

## macOS 2.1.1 workflow

Przygotowanie:

```bash
./scripts/setup-macos.sh --install
```

Build:

```bash
./scripts/build-macos.sh --configuration debug
```

Test:

```bash
./scripts/run-qemu-macos.sh
```

Własna aplikacja:

```bash
./scripts/build-app-macos.sh app.c -o app --install
./scripts/build-macos.sh --configuration debug --stage-only
./scripts/run-qemu-macos.sh --display
```

Po starcie:

```text
run /apps/app
```

Szczegóły: [MACOS_DEVELOPMENT.md](MACOS_DEVELOPMENT.md).

## Validation status for 2.1.1

Patch został przeaudytowany pod kątem zgodności ścieżek Windows/macOS oraz zachowania tego samego formatu artefaktów x86-64. Zależności Homebrew są dostępne dla macOS, ale finalny build i QEMU runtime nowych skryptów muszą zostać wykonane na rzeczywistym Macu. Środowisko wykonujące zmianę w repozytorium nie jest hostem macOS, dlatego sam commit nie jest oznaczany jako świeży macOS runtime PASS.

Akceptacja 2.1.1 na Macu wymaga:

1. `./scripts/setup-macos.sh` — PASS;
2. `./scripts/build-macos.sh --configuration debug --rebuild` — PASS;
3. obecności `dist/KuroganeOS-2.1.1-macos-qemu.img`;
4. `./scripts/run-qemu-macos.sh` — `userspace_init_spawn: PASS` oraz `ALL_REQUIRED_TESTS_PASSED`;
5. opcjonalnie zbudowania własnej aplikacji i uruchomienia jej przez `run /apps/<name>`.

## Known limitations

- pełny recovery environment nadal nie jest zaimplementowany; dostępne są safe mode i diagnostics;
- VirtualBox end-to-end install wymaga osobnej weryfikacji w środowisku z `VBoxManage`;
- real-hardware UEFI pozostaje słabiej zweryfikowany niż QEMU;
- NVMe, audio i szersza obsługa współczesnego sprzętu nie są kompletne;
- desktop i publiczne ABI/SDK pozostają eksperymentalne;
- na Apple Silicon KuroganeOS x86-64 jest emulowany przez QEMU TCG, a nie wykonywany jako natywny ARM guest.

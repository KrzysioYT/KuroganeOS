# Build status

Data: 16 sierpnia 2026 r.

## Current stage

KuroganeOS **2.2.0** jest patchem Desktop Developer Preview na instalowalnym
fundamencie 2.1/2.1.1. Storage, installer oraz macOS backend nie są zastępowane;
2.2 koncentruje się na warstwie użytkowej, shellu i spójności dokumentacji.

## Working foundation

- UEFI `BOOTX64.EFI`, boot protocol v3;
- VMM, GDT/TSS/IST, IDT;
- Ring 3, `int 0x80`, ELF64, PID/TID;
- process spawn/wait/exit i PIT preemption;
- `/system/init` PID 1;
- AHCI, GPT, writable FAT32/VFS, persistent root;
- installer + boot z HDD;
- PS/2 keyboard/mouse, PCI, ACPI/APIC;
- WindowManager i GUI Ring 3;
- Windows/WSL build oraz natywny macOS x86_64-elf/QEMU workflow.

## 2.2 changes

- wersja 2.2.0;
- Kurogane Flux visual language dla framebuffer desktopu;
- Flux Console jako rozbudowany Ring-3 shell;
- `run <name|path>`, `open`, `gui`, `jobs`, `wait`;
- realne `cat/read`, PID/TID, history, cwd, calc, sleep/yield;
- diagnostyczne skróty do Ring-3 System Monitor;
- jawne raportowanie brakujących capability syscalli zamiast `command not found`;
- aktualizacja dokumentów, które nadal opisywały stan 1.0 jako bieżący.

## Build

Windows:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Rebuild
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-installer.ps1 -Configuration release
```

macOS:

```bash
./scripts/setup-macos.sh --install
./scripts/build-macos.sh --configuration debug --rebuild
./scripts/run-qemu-macos.sh --display
```

Ponieważ skrypty pobierają wersję z `common/version.h`, artefakty po tym patchu
używają numeru `2.2.0`.

## Validation

Zmiany Flux Console i renderera zostały sprawdzone statycznie pod kątem
`-Wall -Wextra -Wpedantic -Werror` w środowisku roboczym. Fresh pełny build i
runtime QEMU/VirtualBox nie są deklarowane jako PASS, dopóki nie zostaną
uruchomione właściwym repozytoryjnym toolchainem.

## Known gaps

Najważniejsze kolejne capabilities userspace: `stat/readdir`, writable VFS,
system-info, network sockets i kontrolowany power API. Pełny compositor, resize,
audio, recovery i szerszy hardware pozostają dalszymi etapami.

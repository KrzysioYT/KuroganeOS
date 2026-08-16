# Build status

Data: 16 sierpnia 2026 r.

## Current stage

KuroganeOS **3.2.0 — Red Flux Desktop Shell** jest aktualnym etapem Kurogane
Desktop. Normalny start prowadzi przez graficzny boot splash i userspace session
gate do Red Flux Home. Desktop posiada systemowy dock i session ownership dla
GUI Ring 3.

## Working foundation

- UEFI `BOOTX64.EFI`, boot protocol v3;
- VMM, GDT/TSS/IST, IDT;
- Ring 3, `int 0x80`, ELF64, PID/TID;
- process spawn/wait/exit i PIT preemption;
- `/system/init` jako PID 1;
- AHCI, GPT, writable FAT32/VFS, persistent root;
- PS/2 keyboard/mouse, PCI, ACPI/APIC;
- WindowManager: focus/z-order/drag/resize/minimize/maximize/restore/close;
- software full-frame backbuffer i content clipping;
- Ring-3 `libui` scene/view runtime;
- wspólny `FluxShellCore` dla console i GUI Terminala;
- Windows/WSL build oraz natywny macOS x86_64-elf/QEMU workflow.

## 3.2 changes

- Red Flux jest domyślnym UEFI desktop bootem;
- Safe Mode: `S`/`F8`, Diagnostics: `X`;
- graficzny boot splash z checkpointami paging/Ring3/FS/storage/preemption/PID1;
- automatyczny service-console fallback dla Safe/Diagnostics/Installer/boot fail;
- `/gui/login` jako session gate;
- PID1: `login -> launcher -> login`;
- Red Flux Dock z przypiętymi Home/Terminal/Files/Monitor/Settings/About;
- geometryczne systemowe ikony i running/focus indicators;
- dynamiczna sekcja żywych okien;
- click Dock: focus/restore istniejącego okna albo quick-launch przez Home;
- session ownership: nowe GUI surfaces muszą należeć do drzewa procesu Home;
- legacy Ring-0 surfaces nie uczestniczą w widocznym desktopie;
- anonimowe historyczne `/gui/*` requesty z kernela są kompatybilnościowym no-op;
- nowe tło desktopu, top identity rail, Kurogane brand geometry i chrome;
- login obsługuje Enter oraz kliknięcie myszą.

## Build

macOS:

```bash
./scripts/setup-macos.sh --install
./scripts/build-macos.sh --configuration debug --rebuild
```

Development artifact:

```text
dist/KuroganeOS-3.2.0-macos-qemu.img
```

Windows:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Rebuild
```

Native macOS installer ISO pozostaje osobnym torem i nie jest warunkiem runtime
acceptance development IMG.

## Validation state

Zmiany 3.2 zostały sprawdzone przez audit diffu, ścieżek buildowych i zależności
session/WindowManager/process. **Pełny build oraz runtime QEMU na rzeczywistym
macOS nie są deklarowane jako PASS**, dopóki nie zostaną wykonane na docelowym
cross-toolchainie.

Runtime acceptance 3.2 powinien potwierdzić:

1. normalny boot bez naciskania `D`;
2. boot splash przechodzi do Login;
3. `S`/`F8` nadal otwiera Safe Mode;
4. Enter i klik na Login uruchamia Red Flux Home;
5. po Login nie pojawiają się stare automatycznie uruchomione okna;
6. Dock uruchamia, focusuje i przywraca przypięte aplikacje;
7. dynamiczne task items focusują/minimalizowane okna;
8. drag/resize pozostaje bez ghostingu i widocznego partial-frame flickera.

## Known gaps

- login nie ma jeszcze account/credential service ani hasła;
- compatibility `ku_ui_frame` nadal jest transportem aplikacji;
- brak per-window damage surfaces i natywnego widget hit-test ABI;
- brak pełnego Ring-3 `stat/readdir/write/create/unlink/rename/mkdir/rmdir`;
- brak IPC/settings/notification service;
- brak clipboard/Unicode/audio/NVMe/multi-monitor;
- macOS installer ISO wymaga dalszej walidacji.

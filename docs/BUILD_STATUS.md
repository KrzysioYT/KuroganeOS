# Build status

Data: 16 sierpnia 2026 r.

## Current stage

KuroganeOS **3.1.0 — Red Flux Interaction Update** jest aktualnym etapem
Kurogane Desktop. Normalny boot używa PID1 + Launcher session modelu, a 3.1
koncentruje się na stabilności renderowania, spójnym sterowaniu, wspólnym
shellu i własnej czarno-grafitowo-czerwonej identyfikacji.

## Working foundation

- UEFI `BOOTX64.EFI`, boot protocol v3;
- VMM, GDT/TSS/IST, IDT;
- Ring 3, `int 0x80`, ELF64, PID/TID;
- process spawn/wait/exit i PIT preemption;
- `/system/init` PID 1;
- Launcher jako userspace root sesji desktopowej;
- AHCI, GPT, writable FAT32/VFS, persistent root;
- PS/2 keyboard/mouse, PCI, ACPI/APIC;
- WindowManager: focus/z-order/drag/resize/minimize/maximize/restore/close;
- Signal Spine + Pulse Ribbon;
- Ring-3 `libui` scene/view runtime;
- Windows/WSL build oraz natywny macOS x86_64-elf/QEMU development workflow.

## 3.1 changes

- wersja 3.1.0;
- Red Flux palette: black/graphite/steel/red;
- software full-frame backbuffer do 1600x1200 dla bieżących GOP modes;
- content clipping per window;
- body text scale limit per content area;
- shared `FluxShellCore` dla fallback shell i GUI Terminala;
- GUI Terminal z pełnym parserem fallback shella;
- Up/Down history, Left/Right cursor, Home/End/Delete/Escape;
- publiczne nazwane GUI key codes zamiast magicznych scancode values;
- arrow-first Launcher, Files i Settings;
- `libui` Red Flux jako default dla aplikacji SDK;
- uproszczony compatibility rendering bez `::`, `[> ]` i `>>`;
- README i roadmapa zsynchronizowane z 3.1.

## Build

Windows:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Rebuild
```

macOS:

```bash
./scripts/setup-macos.sh --install
./scripts/build-macos.sh --configuration debug --rebuild
```

Development artifact:

```text
dist/KuroganeOS-3.1.0-macos-qemu.img
```

macOS installer ISO pozostaje osobnym, niezamkniętym torem. Nie jest wymagane do
runtime acceptance Red Flux 3.1.

## Validation state

Repozytorium nie ma obecnie obowiązkowych GitHub status checks dla `main`.
Zmiany 3.1 zostały poddane audytowi zależności, ABI i host-test compatibility,
ale **pełny build oraz runtime QEMU na rzeczywistym Macu nie są deklarowane jako
PASS**, dopóki nie zostaną uruchomione na właściwym macOS cross-toolchainie.

Runtime acceptance 3.1 powinien potwierdzić:

1. drag bez widocznego `clear -> partial frame` flickera;
2. resize bez artefaktów;
3. brak tekstu wychodzącego poza content bounds;
4. Arrow/Enter/Escape/Tab w głównych aplikacjach;
5. identyczne `help`, `calc`, `pwd`, `cat`, `run`, `jobs` itd. w console shell i GUI Terminalu;
6. raw persistent VFS read z `/etc/system.cfg`.

## Known gaps

Najważniejsze następne subsystemy:

- native widget ABI i pointer widget IDs;
- per-window surfaces + compositor damage tracking;
- `stat/readdir/write/create/unlink/rename/mkdir/rmdir` dla Ring 3;
- IPC/settings/notification services;
- userspace sockets/DNS;
- clipboard/wheel/context actions;
- NVMe/audio/multi-monitor i real-hardware qualification;
- dopracowany macOS installer ISO.

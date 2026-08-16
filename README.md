# KuroganeOS 3.1.0

KuroganeOS jest edukacyjnym, 64-bitowym systemem operacyjnym rozwijanym od
podstaw dla x86-64 i UEFI. Nie używa kernela Linux. Aktualna linia **3.1.0 —
Red Flux Interaction Update** rozwija Kurogane Desktop z developer-preview w
spójny, własny interfejs systemowy.

## Kurogane Desktop

Normalny boot używa obecnie modelu:

```text
UEFI
 -> kernel
 -> persistent FAT32 root
 -> WindowManager / Red Flux
 -> /system/init PID 1
 -> /gui/launcher
 -> aplikacje Ring 3 uruchamiane na żądanie
```

PID1 nadzoruje root sesji (`/gui/launcher`). Terminal, Files, System Monitor,
Settings i About są zwykłymi aplikacjami, a nie pięcioma automatycznie
nakładającymi się oknami startowymi.

## 3.1.0 — Red Flux

3.1 koncentruje się na jakości desktopu:

- software full-frame backbuffer dla używanych trybów GOP do 1600x1200;
- gotowa klatka trafia do GOP dopiero po zakończeniu renderu;
- content clipping zapobiega rysowaniu tekstu poza własnym oknem;
- body text scale jest ograniczany zależnie od szerokości content area;
- interactive drag i resize pozostają częścią WindowManagera;
- arrow-first navigation w Launcher/Files/Settings;
- GUI Terminal obsługuje Up/Down history, Left/Right cursor, Home/End, Delete,
  Backspace, Enter i Escape;
- fallback console oraz GUI Terminal korzystają z jednego `FluxShellCore`;
- domyślna identyfikacja wizualna to czerń, grafit, stalowa szarość i czerwień;
- stary cyan/violet/amber developer-preview theme został usunięty z głównej
  ścieżki desktopu;
- `libui` nie renderuje już podstawowych kontrolek jako `[> ... ]`, `>>` i `::`.

Szczegóły: [`docs/releases/3.1.0.md`](docs/releases/3.1.0.md).

## Wspólny FluxShellCore

Recovery shell i aplikacja `/gui/terminal` używają tego samego parsera i tego
samego stanu poleceń. Dostępne są m.in.:

```text
help clear version uname pid whoami status
history jobs wait
pwd cd cat read which apps home
run open gui
hello external files monitor about
mem free tasks pci device driver diskinfo
echo calc sleep yield true false exit
```

Komendy wymagające jeszcze publicznego capability ABI (`ls`, `stat`, `mkdir`,
`rm`, `mv`, `cp`, `ping`, `reboot` itd.) są rozpoznawane, ale nie dostają
backdoora do Ring 0.

## Fundament systemu

- własny `BOOTX64.EFI` i boot protocol v3;
- GDT/TSS/IST, IDT i obsługa wyjątków;
- czteropoziomowe page tables i VMM;
- Ring 3, prywatne przestrzenie adresowe i syscall gate `int 0x80`;
- procesy ELF64, PID/TID, spawn/wait/exit;
- preempcja PIT i scheduler;
- `/system/init` jako PID 1;
- PS/2 keyboard + mouse i wspólna kolejka input;
- PCI, ACPI MADT i APIC discovery;
- SATA/AHCI read/write/flush;
- GPT i writable persistent FAT32/VFS;
- WindowManager z focus/z-order/drag/resize/minimize/maximize/restore/close;
- Signal Spine i Pulse Ribbon;
- Ring-3 UI ABI, `libui` scene/view runtime i desktop applications;
- SDK: `crt0`, `libc`, `libkurogane`, `libui`;
- Windows/WSL build oraz natywny development workflow na macOS.

## Budowanie — macOS

Pierwsze przygotowanie środowiska:

```bash
chmod +x scripts/*.sh
./scripts/setup-macos.sh --install
```

Czysty development build:

```bash
./scripts/build-macos.sh --configuration debug --rebuild
```

Wynik dla aktualnej wersji:

```text
dist/KuroganeOS-3.1.0-macos-qemu.img
```

Development IMG jest aktualnie podstawowym i najlepiej sprawdzonym torem
macOS/QEMU. Native installer ISO pozostaje osobnym torem naprawy i nie jest
traktowane w dokumentacji 3.1 jako runtime-verified medium.

## Uruchamianie w QEMU na macOS

Przykładowa bezpośrednia ścieżka QEMU:

```bash
cp "$(brew --prefix qemu)/share/qemu/edk2-i386-vars.fd" /tmp/kurogane-vars.fd

qemu-system-x86_64 \
  -machine q35,accel=tcg \
  -cpu max \
  -m 768 \
  -vga std \
  -drive if=pflash,format=raw,unit=0,readonly=on,file="$(brew --prefix qemu)/share/qemu/edk2-x86_64-code.fd" \
  -drive if=pflash,format=raw,unit=1,file=/tmp/kurogane-vars.fd \
  -drive if=none,id=kurogane_system,format=raw,file="./dist/KuroganeOS-3.1.0-macos-qemu.img",cache=writeback \
  -device ide-hd,drive=kurogane_system,bus=ide.0,bootindex=1 \
  -display cocoa \
  -serial stdio \
  -net none \
  -no-reboot \
  -no-shutdown
```

Apple Silicon uruchamia x86-64 KuroganeOS przez QEMU TCG.

## Budowanie — Windows

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Rebuild
```

Windows installer release pozostaje obsługiwany przez:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-installer.ps1 -Configuration release
```

## Własna aplikacja na macOS

```bash
./scripts/build-app-macos.sh app.c -o app --install
./scripts/build-macos.sh --configuration debug --stage-only
```

Następnie z Terminala:

```text
run app
```

albo dla GUI:

```text
gui terminal
gui files
```

## Tryby startu

- normalny boot: Kurogane Red Flux Desktop;
- Safe Mode: awaryjna konsola;
- Diagnostics: ograniczony tryb diagnostyczny;
- Installer Mode: instalacja na dysk SATA/AHCI.

## Co nadal nie jest gotowe

3.1 nie jest końcem roadmapy. Najważniejsze otwarte elementy:

- natywny widget ABI zamiast compatibility `ku_ui_frame`;
- per-window surfaces i prawdziwy compositor damage tracking;
- publiczne `stat/readdir/write/create/unlink/rename/mkdir/rmdir`;
- userspace IPC/services;
- publiczne socket API;
- clipboard, wheel routing i context actions;
- audio, NVMe, multi-monitor i szersza kwalifikacja real hardware;
- dopracowany, zweryfikowany installer ISO na macOS.

Pełna roadmapa do 3.6:
[`docs/roadmap/DESKTOP_ROADMAP.md`](docs/roadmap/DESKTOP_ROADMAP.md).

## Dokumentacja

- [`docs/releases/3.1.0.md`](docs/releases/3.1.0.md)
- [`docs/roadmap/DESKTOP_ROADMAP.md`](docs/roadmap/DESKTOP_ROADMAP.md)
- [`docs/GUI.md`](docs/GUI.md)
- [`docs/USERSPACE.md`](docs/USERSPACE.md)
- [`docs/BUILD_STATUS.md`](docs/BUILD_STATUS.md)
- [`docs/MACOS_DEVELOPMENT.md`](docs/MACOS_DEVELOPMENT.md)
- [`docs/INSTALLATION.md`](docs/INSTALLATION.md)
- [`docs/VIRTUALBOX_TESTING.md`](docs/VIRTUALBOX_TESTING.md)

## Licencja

Aktualne rewizje KuroganeOS są udostępniane na warunkach **KuroganeOS
Source-Available License 1.0 (KSAL-1.0)**. Jest to licencja source-available,
a nie licencja Open Source zatwierdzona przez OSI.

- [`LICENSE`](LICENSE)
- [`LICENSE-MIT-LEGACY`](LICENSE-MIT-LEGACY)
- [`docs/LICENSING.md`](docs/LICENSING.md)
- [`CLA.md`](CLA.md)
- [`CONTRIBUTING.md`](CONTRIBUTING.md)

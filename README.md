# KuroganeOS 3.2.0

KuroganeOS jest edukacyjnym, 64-bitowym systemem operacyjnym rozwijanym od
podstaw dla x86-64 i UEFI. Nie korzysta z kernela Linux. Linia **3.2.0 — Red
Flux Desktop Shell** rozwija własny desktop KuroganeOS: normalny boot graficzny,
boot splash, bramę sesji, pełniejszy dock oraz spójny czarno-grafitowo-czerwony
język wizualny.

## Normalny model systemu

```text
UEFI BOOTX64.EFI
 -> Red Flux jako domyślny boot
 -> kernel + boot splash
 -> persistent FAT32 root
 -> WindowManager / software backbuffer
 -> /system/init (PID 1)
 -> /gui/login
 -> /gui/launcher (Red Flux Home)
 -> aplikacje Ring 3
```

Login jest obecnie bramą lokalnej sesji deweloperskiej. Nie udaje jeszcze
prawdziwego uwierzytelniania hasłem — credential store i account service są
osobnym przyszłym subsystemem.

## 3.2.0 — Red Flux Desktop Shell

Najważniejsze zmiany:

- Red Flux jest normalnym bootem; nie trzeba naciskać `D`;
- UEFI pozostawia `S`/`F8` dla Safe Mode i `X` dla Diagnostics;
- framebuffer podczas normalnego startu pokazuje graficzny boot splash i
  progres rzeczywistych checkpointów kernela;
- Safe Mode, Diagnostics, Installer i fatalny boot error wracają do konsoli
  serwisowej;
- `/gui/login` oddziela boot od właściwej sesji desktopowej;
- PID1 nadzoruje model `Login -> Home -> Login`;
- dolny Pulse Ribbon został przebudowany w **Red Flux Dock**;
- Dock ma przypięte Home, Terminal, Files, Monitor, Settings i About;
- ikony Docka są rysowane przez własne prymitywy KuroganeOS;
- uruchomione aplikacje mają running/focus state;
- dynamiczna część Docka obsługuje focus/restore żywych okien;
- WindowManager wymusza ownership GUI przez drzewo procesu Red Flux Home;
- stare anonimowe Ring-0 GUI autospawny są wygaszane kompatybilnościowo;
- pulpit ma nowe tło, top identity rail, geometryczny znak Kurogane i
  odświeżony window chrome;
- software backbuffer, clipping i stabilniejszy drag/resize z 3.1 pozostają
  fundamentem renderowania.

Szczegóły: [`docs/releases/3.2.0.md`](docs/releases/3.2.0.md).

## Sterowanie desktopem

- mysz — focus, drag, resize, window controls i Dock;
- `Arrow Up/Down/Left/Right` — nawigacja w aplikacjach;
- `Enter` — aktywacja;
- `Escape` — anulowanie lokalnej akcji;
- `Tab` — focus/selection traversal tam, gdzie aplikacja go obsługuje;
- `Alt+Tab` — zmiana aktywnego okna;
- `Alt+F4` — zamknięcie aktywnego okna.

`J/K` mogą pozostawać jako aliasy w części starszych aplikacji, ale nie są już
głównym modelem sterowania.

## FluxShellCore

Fallback console i `/gui/terminal` korzystają z jednego silnika poleceń.
Dostępne są m.in.:

```text
help clear version uname pid whoami status
history jobs wait
pwd cd cat read which apps home
run open gui
hello external files monitor about
echo calc sleep yield true false exit
```

Komendy wymagające jeszcze rozszerzonego capability ABI są rozpoznawane bez
backdoora do Ring 0.

## Fundament systemu

- własny UEFI loader i boot protocol v3;
- GDT/TSS/IST, IDT i wyjątki x86-64;
- czteropoziomowe page tables i VMM;
- Ring 3, prywatne address spaces i `int 0x80` syscall ABI;
- ELF64, PID/TID, spawn/wait/exit i preempcja PIT;
- `/system/init` jako PID 1;
- AHCI SATA, GPT i writable persistent FAT32/VFS;
- PS/2 keyboard/mouse i wspólna kolejka input;
- PCI, ACPI MADT i APIC discovery;
- WindowManager z focus/z-order/drag/resize/minimize/maximize/restore/close;
- software full-frame backbuffer dla typowych trybów GOP;
- Ring-3 UI ABI i `libui` scene/view runtime;
- SDK: `crt0`, `libc`, `libkurogane`, `libui`;
- build na Windows/WSL oraz natywny development workflow na macOS.

## Build — macOS

Pierwsze przygotowanie:

```bash
chmod +x scripts/*.sh
./scripts/setup-macos.sh --install
```

Czysty development build:

```bash
./scripts/build-macos.sh --configuration debug --rebuild
```

Wynik:

```text
dist/KuroganeOS-3.2.0-macos-qemu.img
```

Development IMG pozostaje podstawowym torem macOS/QEMU. Native installer ISO
nie jest w tej dokumentacji oznaczane jako runtime-verified medium.

## QEMU — macOS

```bash
cp "$(brew --prefix qemu)/share/qemu/edk2-i386-vars.fd" /tmp/kurogane-vars.fd

qemu-system-x86_64 \
  -machine q35,accel=tcg \
  -cpu max \
  -m 768 \
  -vga std \
  -drive if=pflash,format=raw,unit=0,readonly=on,file="$(brew --prefix qemu)/share/qemu/edk2-x86_64-code.fd" \
  -drive if=pflash,format=raw,unit=1,file=/tmp/kurogane-vars.fd \
  -drive if=none,id=kurogane_system,format=raw,file="./dist/KuroganeOS-3.2.0-macos-qemu.img",cache=writeback \
  -device ide-hd,drive=kurogane_system,bus=ide.0,bootindex=1 \
  -display cocoa \
  -serial stdio \
  -net none \
  -no-reboot \
  -no-shutdown
```

Na Apple Silicon KuroganeOS pozostaje gościem x86-64 uruchamianym przez QEMU
TCG.

## Build — Windows

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Rebuild
```

Installer release:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-installer.ps1 -Configuration release
```

## Tryby startu

- normalny boot — Red Flux Boot -> Login -> Desktop;
- `S` / `F8` — Safe Mode;
- `X` — Diagnostics;
- Installer Mode — instalacja na dysk SATA/AHCI.

## Następne duże subsystemy

- natywny widget ABI zamiast compatibility `ku_ui_frame`;
- per-window surfaces i damage compositor;
- prawdziwa account service + credential store + lock screen;
- publiczne `stat/readdir/write/create/unlink/rename/mkdir/rmdir`;
- userspace IPC/services;
- sockets/DNS dla userspace;
- clipboard, Unicode, audio, NVMe i szersza kwalifikacja sprzętowa.

## Dokumentacja

- [`docs/releases/3.2.0.md`](docs/releases/3.2.0.md)
- [`docs/roadmap/DESKTOP_ROADMAP.md`](docs/roadmap/DESKTOP_ROADMAP.md)
- [`docs/GUI.md`](docs/GUI.md)
- [`docs/BUILD_STATUS.md`](docs/BUILD_STATUS.md)
- [`docs/MACOS_DEVELOPMENT.md`](docs/MACOS_DEVELOPMENT.md)
- [`docs/INSTALLATION.md`](docs/INSTALLATION.md)

## Licencja

Aktualne rewizje KuroganeOS są udostępniane na warunkach **KuroganeOS
Source-Available License 1.0 (KSAL-1.0)**. Nie jest to licencja Open Source
zatwierdzona przez OSI.

- [`LICENSE`](LICENSE)
- [`LICENSE-MIT-LEGACY`](LICENSE-MIT-LEGACY)
- [`docs/LICENSING.md`](docs/LICENSING.md)
- [`CLA.md`](CLA.md)
- [`CONTRIBUTING.md`](CONTRIBUTING.md)

# KuroganeOS 2.3.0

KuroganeOS jest edukacyjnym, 64-bitowym systemem operacyjnym rozwijanym od
podstaw dla x86-64 i UEFI. Nie używa kernela Linux. Wydanie **2.3.0** jest
**Desktop Boot Repair**: istniejący WindowManager i aplikacje Ring-3 GUI są
teraz spięte w normalną sesję systemu zamiast pozostawać za ręcznym trybem
Desktop Alpha.

## 2.3.0 — Flux Desktop jako normalna sesja

Normalny userspace boot uruchamia teraz kernelowy host `flux-session`, który
inicjalizuje WindowManager i prowadzi rendering/input desktopu. Następnie
`/system/init` jako PID 1 uruchamia i nadzoruje:

```text
/gui/terminal
/gui/files
/gui/sysmon
/gui/settings
/gui/about
```

Poprawny start sesji powinien raportować:

```text
[TEST] desktop_session: PASS
[TEST] userspace_init_spawn: PASS
[TEST] desktop_userspace_apps: PASS
[TEST] userspace_desktop_session: PASS
```

Jeżeli większość aplikacji GUI kończy się natychmiast podczas początkowego
probe, PID1 przechodzi do `/apps/shell` jako console fallback. Safe Mode nadal
pozostaje tekstowym środowiskiem awaryjnym.

Szczegóły: [`docs/releases/2.3.0.md`](docs/releases/2.3.0.md).

## Kurogane Flux

Kurogane Flux ma być własnym językiem desktopowym zamiast kopią Windows,
macOS, GNOME, KDE czy innego środowiska.

Aktualna warstwa UI posiada już:

- software framebuffer rendering;
- WindowManager z focus, z-order i drag;
- minimize/maximize/restore/close;
- Alt+Tab i Alt+F4;
- software cursor;
- Ring-3 UI ABI i `libui`;
- wieloprocesowe aplikacje desktopowe.

**2.3 naprawia lifecycle i uruchamianie GUI.** Obecny WindowManager nadal ma
część starego chrome/taskbar modelu. Pełna przebudowa tej warstwy na Signal
Spine, Pulse Ribbon i własne Flux window controls jest celem 2.4.

Pełna roadmapa 2.3 → 3.6:
[`docs/roadmap/DESKTOP_ROADMAP.md`](docs/roadmap/DESKTOP_ROADMAP.md).

## Flux Console

Tekstowa Flux Console pozostaje dostępna jako aplikacja/fallback i obsługuje
m.in.:

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
run test
gui terminal
jobs
wait <pid>
```

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

## macOS FAT32 validation

Macowy builder przed publikacją development IMG przepuszcza obraz przez
projektowy test GPT/PartitionDevice/FAT32/VFS. Oczekiwane markery:

```text
Foundation root PartitionDevice/FAT32/VFS read: PASS
[macos] Foundation root FAT32/VFS validation: PASS
```

## Budowanie — macOS

Pierwsze przygotowanie:

```bash
chmod +x scripts/*.sh
./scripts/setup-macos.sh --install
```

Development IMG:

```bash
./scripts/build-macos.sh --configuration debug --rebuild
```

Wynik:

```text
dist/KuroganeOS-2.3.0-macos-qemu.img
```

IMG + instalowalne ISO:

```bash
./scripts/build-macos.sh --configuration release --rebuild --iso
```

Wyniki:

```text
dist/KuroganeOS-2.3.0-macos-qemu.img
dist/KuroganeOS-2.3.0-x86_64.iso
dist/SHA256SUMS.txt
kurogane.iso
```

Tylko ISO:

```bash
./scripts/build-installer-macos.sh --configuration release --rebuild
```

Stara komenda zgodności również działa na macOS:

```bash
./scripts/build-iso.sh release
```

Test GUI:

```bash
./scripts/run-qemu-macos.sh \
  --image ./dist/KuroganeOS-2.3.0-macos-qemu.img \
  --display
```

Własna aplikacja:

```bash
./scripts/build-app-macos.sh app.c -o app --install
./scripts/build-macos.sh --configuration debug --stage-only
./scripts/run-qemu-macos.sh --display
```

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
dist/KuroganeOS-2.3.0-x86_64.iso
dist/SHA256SUMS.txt
```

## Tryby startu

- normalny boot: Kurogane Flux Desktop session;
- Safe Mode: emergency kernel console;
- Diagnostics: ograniczony tryb diagnostyczny;
- Installer Mode: instalacja na dysk SATA/AHCI.

Historyczny prompt bootloadera może nadal wspominać `boot=console` i `DESKTOP
ALPHA`; od 2.3 normalny userspace path nie zależy już od ręcznego `D`. Cleanup
tego legacy boot/UI textu jest częścią 2.4.

## Znane ograniczenia

KuroganeOS 2.3 nadal jest Developer Preview. Najważniejsze braki:

- brak pełnego compositora GPU i resize wszystkich surfaces;
- publiczne UI ABI nadal opiera się na stałym `ku_ui_frame`;
- brak kompletnego userspace `stat/readdir` i writable VFS capability ABI;
- brak pełnego userspace socket API;
- brak audio, pełnego recovery oraz szerokiej kwalifikacji real hardware;
- część WindowManager chrome nadal pochodzi z Desktop Alpha;
- publiczne ABI/SDK nadal może ewoluować;
- Apple Silicon uruchamia x86-64 KuroganeOS przez QEMU TCG.

## Dokumentacja

- [`docs/releases/2.3.0.md`](docs/releases/2.3.0.md)
- [`docs/releases/2.2.6.md`](docs/releases/2.2.6.md)
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

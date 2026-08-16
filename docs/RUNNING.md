# Uruchamianie KuroganeOS 3.3.3-dev

Ten dokument jest główną instrukcją uruchamiania systemu po zbudowaniu.
Jeżeli nie wiesz, czym różni się IMG od ISO, użyj sekcji **Najprostsza opcja**.

> KuroganeOS jest systemem x86-64 UEFI. Nie używaj trybu legacy BIOS.

## Najprostsza opcja

Po pełnym buildzie masz dwa główne nośniki:

```text
dist/KuroganeOS-3.3.3-dev-<host>-qemu.img
dist/KuroganeOS-3.3.3-dev-x86_64.iso
```

- **IMG**: najlepszy do szybkiego testowania w QEMU.
- **ISO**: najlepszy do VirtualBox oraz testowania `Try / Install` jak zwykłego nośnika instalacyjnego.

Po starcie nośnika wybierz `Try KuroganeOS`, jeżeli chcesz tylko uruchomić
system bez instalacji.

---

# Windows 11 + WSL

## 1. Wymagane pliki Windows

Windows potrzebuje repozytoryjnego toolchainu z paczki:

https://drive.google.com/file/d/1sHfNdDOOVeJh3Q0FOtUlqPbHZIZ-ykEk/view?usp=sharing

Po wypakowaniu musi istnieć:

```text
tools\compiler\x86_64-elf\bin\x86_64-elf-g++.exe
```

WSL również musi być zainstalowany.

## 2. Repo może być na C:, D:, I: lub innym dysku

3.3.x zawiera `scripts/wsl-path.ps1`. Jeżeli standardowy `wslpath` nie widzi
np. `I:\KuroganeOS`, build próbuje automatycznie zamontować dysk przez DrvFs.

Jeżeli WSL nadal nie widzi dysku:

```powershell
wsl.exe --shutdown
```

i uruchom build ponownie.

## 3. Pełny build IMG + ISO

```powershell
cd I:\KuroganeOS

git pull origin main
wsl.exe --shutdown

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\build-media.ps1 `
  -Configuration release `
  -Rebuild
```

Oczekiwane pliki:

```text
dist\KuroganeOS-3.3.3-dev-windows-qemu.img
dist\KuroganeOS-3.3.3-dev-x86_64.iso
```

## 4. Uruchom IMG w QEMU

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\run-qemu.ps1 `
  -UseDiskImage `
  -DiskImagePath .\dist\KuroganeOS-3.3.3-dev-windows-qemu.img `
  -Display `
  -KeepRunning `
  -MemoryMiB 1024
```

## 5. Uruchom ISO w QEMU

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\run-qemu.ps1 `
  -UseIso `
  -Display `
  -KeepRunning `
  -MemoryMiB 1024
```

---

# macOS

## 1. Przygotowanie

```bash
cd /Users/$USER/Documents/Github/KuroganeOS
chmod +x scripts/*.sh
./scripts/setup-macos.sh --install
```

## 2. Pełny build IMG + ISO

```bash
git pull origin main
chmod +x scripts/*.sh

bash ./scripts/build-media-macos.sh \
  --configuration release \
  --rebuild
```

Oczekiwane:

```text
dist/KuroganeOS-3.3.3-dev-macos-qemu.img
dist/KuroganeOS-3.3.3-dev-x86_64.iso
```

## 3. Uruchom IMG

Okno:

```bash
./scripts/run-qemu-macos.sh \
  --image ./dist/KuroganeOS-3.3.3-dev-macos-qemu.img \
  --windowed
```

Pełny ekran:

```bash
./scripts/run-qemu-macos.sh \
  --image ./dist/KuroganeOS-3.3.3-dev-macos-qemu.img \
  --display
```

Na Apple Silicon KuroganeOS x86-64 działa przez QEMU TCG. VirtualBox nie jest
referencyjną ścieżką dla tego gościa na Apple Silicon.

## 4. Bezpośredni QEMU, gdy wrapper sprawia problem

```bash
cp "$(brew --prefix qemu)/share/qemu/edk2-i386-vars.fd" /tmp/kurogane-vars.fd

qemu-system-x86_64 \
  -machine q35,accel=tcg \
  -cpu max \
  -m 1024 \
  -vga std \
  -drive if=pflash,format=raw,unit=0,readonly=on,file="$(brew --prefix qemu)/share/qemu/edk2-x86_64-code.fd" \
  -drive if=pflash,format=raw,unit=1,file=/tmp/kurogane-vars.fd \
  -drive if=none,id=kurogane_system,format=raw,file="./dist/KuroganeOS-3.3.3-dev-macos-qemu.img",cache=writeback \
  -device ide-hd,drive=kurogane_system,bus=ide.0,bootindex=1 \
  -device e1000,netdev=net0 \
  -netdev user,id=net0 \
  -audiodev coreaudio,id=audio0 \
  -device AC97,audiodev=audio0 \
  -display cocoa \
  -serial stdio \
  -no-reboot \
  -no-shutdown
```

---

# Linux x86-64

## Build

```bash
bash ./scripts/setup-linux.sh --install
bash ./scripts/build-media-linux.sh --configuration release --rebuild
```

Wynik:

```text
dist/KuroganeOS-3.3.3-dev-linux-qemu.img
dist/KuroganeOS-3.3.3-dev-x86_64.iso
```

---

# VirtualBox x86-64

Używaj:

```text
dist/KuroganeOS-3.3.3-dev-x86_64.iso
```

Ustawienia referencyjne:

```text
Firmware:     EFI / UEFI
RAM:          1024 MiB
CPU:          1-2
Storage:      Intel AHCI SATA
Optical:      KuroganeOS ISO
Boot order:   DVD, potem HDD
Network:      NAT
Adapter:      Intel PRO/1000 MT Desktop (82540EM)
Audio:        Intel AC'97
Secure Boot:  OFF
```

Pełna instrukcja: [`VIRTUALBOX.md`](VIRTUALBOX.md).

---

# Co powinno pojawić się po uruchomieniu 3.3.3

```text
UEFI
 -> KuroganeOS bootloader
 -> Red Flux boot splash
 -> Try / Install
 -> Login
 -> Red Flux Desktop
      -> HOME shortcut
      -> PERFORMANCE shortcut
      -> Dock
      -> Performance live widget po prawej stronie
```

Home pozostaje procesem-rootem sesji. Zamknięcie jego okna nie wylogowuje.

## Performance

Po wejściu do desktopu `/gui/performance` uruchamia się automatycznie i
WindowManager ustawia go w środkowo-prawej części workspace.

Pokazuje na żywo:

```text
CPU %
GPU/GFX %
RAM %
DISK ACTIVITY %
RAM used / total
uptime ticks
```

`GPU/GFX %` oznacza obciążenie aktualnego stosu GOP/software compositor, a nie
wykorzystanie rdzeni fizycznego GPU.

## Przypinanie aplikacji do pulpitu

Otwórz Home, wybierz aplikację strzałkami i naciśnij:

```text
P
```

aby ją przypiąć lub odpiąć od pulpitu.

Home jest zawsze przypięty, Performance jest przypięte domyślnie. W 3.3.3 stan
pozostałych pinów jest sesyjny; zapis na dysk pojawi się z persistent settings
service.

## Kurogane Web

Z Home/Dock uruchom `Kurogane Web`. Przeglądarka 3.3.3 używa własnego stosu:

```text
E1000 -> DHCP -> DNS -> TCP -> HTTP/1.0
```

Przykładowy adres:

```text
http://example.com/
```

Ograniczenia DEV BETA:

- tylko `http://`;
- brak TLS/HTTPS;
- maksymalnie 4096 B odpowiedzi w jednym żądaniu;
- prosty tekstowy rendering HTML;
- nie jest to jeszcze Chromium.

---

# Najczęstsze problemy

## `Unable to convert path for WSL: I:\...`

```powershell
git pull origin main
wsl.exe --shutdown
```

Następnie ponownie uruchom `build-media.ps1`.

## macOS: `Permission denied` dla `scripts/*.sh`

```bash
chmod +x scripts/*.sh
```

## VirtualBox: `No bootable medium`

Sprawdź:

1. EFI/UEFI jest włączone;
2. ISO jest podpięte jako DVD;
3. DVD jest przed HDD;
4. używasz `KuroganeOS-3.3.3-dev-x86_64.iso`;
5. Secure Boot jest wyłączony.

ISO przechodzi obowiązkowy 20-pass verifier i optical UEFI smoke w CI.

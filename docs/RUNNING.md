# Uruchamianie KuroganeOS 3.3.1-dev

Ten dokument jest główną instrukcją uruchamiania systemu po zbudowaniu.
Jeżeli nie wiesz, czym różni się IMG od ISO, użyj sekcji **Najprostsza opcja**.

> KuroganeOS jest systemem x86-64 UEFI. Nie używaj trybu legacy BIOS.

## Najprostsza opcja

Po pełnym buildzie masz dwa główne nośniki:

```text
dist/KuroganeOS-3.3.1-dev-<host>-qemu.img
dist/KuroganeOS-3.3.1-dev-x86_64.iso
```

- **IMG**: najlepszy do szybkiego testowania w QEMU.
- **ISO**: najlepszy do VirtualBox oraz testowania `Try / Install` jak zwykłego nośnika instalacyjnego.

Po starcie nośnika wybierz:

```text
Try KuroganeOS
```

jeżeli chcesz tylko uruchomić system bez instalacji.

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

3.3.1 zawiera `scripts/wsl-path.ps1`. Jeżeli standardowy `wslpath` nie widzi
np. `I:\KuroganeOS`, build spróbuje automatycznie zamontować dysk `I:` przez
DrvFs jako:

```text
/mnt/i
```

Dlatego nie trzeba przenosić repozytorium na `C:` tylko z powodu WSL.

Jeżeli WSL nie ma dostępu do dysku, uruchom w PowerShell:

```powershell
wsl.exe --shutdown
```

a następnie ponów build. Jeżeli dysk jest zasobem sieciowym z ograniczonymi
uprawnieniami, najpewniejszy jest lokalny NTFS/exFAT dostępny dla WSL.

## 3. Pełny build IMG + ISO

W katalogu repo:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\build-media.ps1 `
  -Configuration release `
  -Rebuild
```

Oczekiwane pliki:

```text
dist\KuroganeOS-3.3.1-dev-windows-qemu.img
dist\KuroganeOS-3.3.1-dev-x86_64.iso
```

## 4. Uruchom IMG w QEMU

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\run-qemu.ps1 `
  -UseDiskImage `
  -DiskImagePath .\dist\KuroganeOS-3.3.1-dev-windows-qemu.img `
  -Display `
  -KeepRunning `
  -MemoryMiB 1024
```

## 5. Uruchom ISO w QEMU

Pełny build zapisuje również kompatybilny `kurogane.iso` w katalogu głównym.

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

## 2. Pełny build

```bash
bash ./scripts/build-media-macos.sh --configuration release --rebuild
```

Oczekiwane artefakty:

```text
dist/KuroganeOS-3.3.1-dev-macos-qemu.img
dist/KuroganeOS-3.3.1-dev-x86_64.iso
```

## 3. Uruchom IMG

Wariant okienkowy:

```bash
./scripts/run-qemu-macos.sh \
  --image ./dist/KuroganeOS-3.3.1-dev-macos-qemu.img \
  --windowed
```

Pełny ekran:

```bash
./scripts/run-qemu-macos.sh \
  --image ./dist/KuroganeOS-3.3.1-dev-macos-qemu.img \
  --display
```

Na Apple Silicon KuroganeOS x86-64 działa przez QEMU TCG. VirtualBox na takim
hoście nie jest referencyjnym środowiskiem KuroganeOS x86-64.

## 4. Bezpośredni QEMU — gdy wrapper sprawia problem

```bash
cp "$(brew --prefix qemu)/share/qemu/edk2-i386-vars.fd" /tmp/kurogane-vars.fd

qemu-system-x86_64 \
  -machine q35,accel=tcg \
  -cpu max \
  -m 1024 \
  -vga std \
  -drive if=pflash,format=raw,unit=0,readonly=on,file="$(brew --prefix qemu)/share/qemu/edk2-x86_64-code.fd" \
  -drive if=pflash,format=raw,unit=1,file=/tmp/kurogane-vars.fd \
  -drive if=none,id=kurogane_system,format=raw,file="./dist/KuroganeOS-3.3.1-dev-macos-qemu.img",cache=writeback \
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

## 1. Zależności

```bash
bash ./scripts/setup-linux.sh --install
```

## 2. Build

```bash
bash ./scripts/build-media-linux.sh --configuration release --rebuild
```

Oczekiwane:

```text
dist/KuroganeOS-3.3.1-dev-linux-qemu.img
dist/KuroganeOS-3.3.1-dev-x86_64.iso
```

## 3. QEMU

Ścieżka do OVMF zależy od dystrybucji. Na Ubuntu/Debian często dostępne są:

```text
/usr/share/OVMF/OVMF_CODE_4M.fd
/usr/share/OVMF/OVMF_VARS_4M.fd
```

Przykład:

```bash
cp /usr/share/OVMF/OVMF_VARS_4M.fd /tmp/kurogane-vars.fd

qemu-system-x86_64 \
  -machine q35 \
  -m 1024 \
  -vga std \
  -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,unit=1,file=/tmp/kurogane-vars.fd \
  -drive if=none,id=kurogane_system,format=raw,file=./dist/KuroganeOS-3.3.1-dev-linux-qemu.img \
  -device ide-hd,drive=kurogane_system,bus=ide.0,bootindex=1 \
  -device e1000,netdev=net0 \
  -netdev user,id=net0 \
  -device AC97 \
  -serial stdio
```

---

# VirtualBox x86-64

Do VirtualBox używaj ISO:

```text
dist/KuroganeOS-3.3.1-dev-x86_64.iso
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

Windows może też automatycznie utworzyć VM:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\create-virtualbox-vm.ps1 `
  -Iso .\dist\KuroganeOS-3.3.1-dev-x86_64.iso
```

---

# Co powinno pojawić się po uruchomieniu

Prawidłowa ścieżka 3.3.1-dev:

```text
UEFI
 -> KuroganeOS bootloader
 -> Red Flux boot splash
 -> Try / Install (dla media instalacyjnego)
 -> Login
 -> Red Flux Home
 -> Dock
```

Dock ma stałe piny:

```text
Home | Terminal | Files | Monitor | Settings | About
```

Kliknięcie uruchomionej aplikacji ma ją przywrócić/focusować. Kliknięcie pinu
aplikacji, która jeszcze nie działa, przekazuje żądanie do Red Flux Home i ją
uruchamia.

---

# Najczęstsze problemy

## `Unable to convert path for WSL: I:\...`

Zrób najpierw:

```powershell
git pull origin main
wsl.exe --shutdown
```

Następnie ponownie uruchom `build-media.ps1`. Aktualny `wsl-path.ps1` próbuje
sam zamontować brakujący dysk Windows przez DrvFs.

## macOS: `Permission denied` dla `scripts/*.sh`

```bash
chmod +x scripts/*.sh
```

## VirtualBox: `No bootable medium`

Sprawdź kolejno:

1. EFI/UEFI jest włączone;
2. ISO jest w napędzie optycznym;
3. DVD jest przed HDD w boot order;
4. używasz wersjonowanego `KuroganeOS-3.3.1-dev-x86_64.iso`;
5. Secure Boot jest wyłączony.

ISO 3.3.1 przechodzi obowiązkowy 20-pass verifier oraz niezależny optical UEFI
smoke w CI przed uznaniem struktury nośnika za poprawną.

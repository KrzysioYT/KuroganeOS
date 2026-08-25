# Uruchamianie KuroganeOS 3.3.3-dev

Ten plik jest **kanoniczną instrukcją uruchamiania bieżącego development builda**.
Aktualny publiczny numer wersji nadal wynosi `3.3.3-dev`; gałąź GUI rozwija
warstwę Forged Steel/KuroganeOS 5, ale nie oznaczamy jej jako gotowego 5.0.0.

KuroganeOS jest gościem **x86-64 UEFI**. Nie uruchamiaj go w trybie legacy BIOS.

## Którego obrazu użyć?

Po zwykłym buildzie deweloperskim najważniejsze są:

```text
build/images/KuroganeOS-base.img   # czysty Foundation GPT + ESP + Kurogane Root
state/KuroganeOS.img               # zachowywany working image, jeżeli istnieje
kurogane.img                        # legacy 64 MiB FAT/EFI artifact
```

Do normalnego desktopu, PID 1, `/system/init`, loginu i aplikacji Ring-3 używaj
**Foundation GPT** (`build/images/KuroganeOS-base.img`) albo working image.
`kurogane.img` nie ma partycji `Kurogane Root` i nie jest pełnym dyskiem
userspace.

Po `build-media.ps1` na Windows powstają media dystrybucyjne:

```text
dist/KuroganeOS-3.3.3-dev-qemu-x86_64.img
dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
dist/SHA256SUMS.txt
```

## Windows 11 — najszybsza ścieżka developerska

### 1. Build

```powershell
cd E:\KuroganeOS

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\build.ps1 `
  -Configuration debug `
  -Rebuild
```

Windows build wymaga repozytoryjnego cross-toolchainu w:

```text
tools\compiler\x86_64-elf\bin\
```

oraz WSL2 dla host tests, FAT/GPT i części media tooling.

### 2. Interaktywny QEMU — zalecane

Dla pracy nad GUI na Windows użyj szybkiego runnera:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\run-qemu-fast.ps1 `
  -MemoryMiB 1024 `
  -Accelerator auto `
  -LogName forged-dev
```

Jeżeli istnieje `state/KuroganeOS.img`, runner wybiera go automatycznie. W
przeciwnym razie używa `build/images/KuroganeOS-base.img`.

`-Accelerator auto` najpierw próbuje **WHPX**. Jeżeli host nie udostępnia WHPX,
runner wraca do **TCG**. W terminalu sprawdź:

```text
[active] accelerator=whpx
```

TCG jest funkcjonalne, ale software framebuffer/compositor może działać znacznie
wolniej.

### 3. Deterministyczny QEMU/test runner

Do testów i logów używaj `run-qemu.ps1`/`run-qemu.sh`, nie `run-qemu-fast.ps1`.
Foundation test:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\run-qemu.ps1 `
  -UseDiskImage `
  -DiskImagePath .\build\images\KuroganeOS-base.img `
  -ShellTest `
  -TimeoutSeconds 90 `
  -MemoryMiB 1024 `
  -LogName foundation-test
```

`ShellTest` jest nazwą kompatybilności historycznej. Na bieżącym Foundation
obrazie rozpoznaje **graficzny login**, aktywuje live session i weryfikuje
markery PID1/login/Blade zamiast bezwarunkowo czekać na stary prompt tekstowy.

### 4. WSL wrapper

Z WSL:

```bash
./scripts/run-qemu.sh interactive
./scripts/run-qemu.sh system
./scripts/run-qemu.sh safe
./scripts/run-qemu.sh fast
```

`interactive` wybiera working image, a potem base image. `system` zawsze używa
czystego Foundation base, aby wynik testu nie zależał od danych użytkownika.

## Pełne media Windows

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\build-media.ps1 `
  -Configuration release `
  -Rebuild
```

Wyniki:

```text
dist\KuroganeOS-3.3.3-dev-qemu-x86_64.img
dist\KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
dist\SHA256SUMS.txt
```

Nie używaj starych nazw `*-windows-qemu.img` ani ogólnego `*-x86_64.iso` jako
bieżącego kontraktu Windows. Builder usuwa te niejednoznaczne aliasy.

## VirtualBox x86-64

Użyj:

```text
dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
```

Referencyjna VM:

```text
Firmware:      EFI / UEFI
Secure Boot:   OFF
RAM:           1024-2048 MiB
CPU:           1-2
System disk:   SATA / Intel AHCI
Optical ISO:   IDE/DVD
Network:       NAT
NIC:           Intel PRO/1000 MT Desktop (82540EM)
Audio:         Intel AC'97
Input:         PS/2
```

Pełna instrukcja: [VIRTUALBOX.md](VIRTUALBOX.md).

## macOS

Pierwsze przygotowanie:

```bash
chmod +x scripts/*.sh
bash ./scripts/setup-macos.sh --install
```

Build:

```bash
bash ./scripts/build-media-macos.sh --configuration debug --rebuild
```

Interaktywny QEMU:

```bash
./scripts/run-qemu-macos.sh \
  --image ./build/images/KuroganeOS-base.img \
  --windowed \
  --memory 1024
```

Możesz pominąć `--image`; runner szuka kolejno aktualnego obrazu `dist`, working
image oraz Foundation base.

Na Apple Silicon gość x86-64 działa przez **QEMU TCG**, więc GUI będzie wyraźnie
wolniejsze niż na Windows/WHPX. To nie jest właściwe środowisko do mierzenia
FPS compositora.

## Linux x86-64

Build media:

```bash
bash ./scripts/setup-linux.sh --install
bash ./scripts/build-media-linux.sh --configuration release --rebuild
```

Do szybkiego testu z hosta Linux preferuj zwykłe `qemu-system-x86_64` z KVM,
jeżeli host i konfiguracja na to pozwalają. WSL wrapper jest przeznaczony dla
Windows + WSL, ponieważ deleguje do `powershell.exe`.

## Co powinno pojawić się po starcie Foundation

Bieżąca ścieżka desktopowa:

```text
UEFI
 -> BOOTX64.EFI
 -> kernel
 -> Forged Steel boot splash
 -> PID 1: /system/init
 -> KUROGANE // SECURE ACCESS
 -> ENTER KUROGANE DESKTOP
 -> Blade Launcher session root
 -> desktop / dock / aplikacje Ring-3
```

Najważniejsze aplikacje:

```text
Blade Launcher
Kurosh
Vault
Anvil
Forge Control
Pulse
Kurogane Web
Performance
System Monitor
About
```

## Kurogane Web

Bieżąca aplikacja Web obsługuje własny stos KuroganeOS i ma transport HTTP oraz
HTTPS/TLS. Nie jest Chromium. Ma prosty natywny parser/rendering HTML/CSS,
historię, Back/Home/Reload, redirecty oraz aktywację linków.

Do testu sieci używaj Foundation/base image z E1000/NAT, nie legacy
`kurogane.img`.

## Logi

Windows QEMU zapisuje:

```text
build/logs/<LogName>-serial.log
build/logs/<LogName>-stdout.log
build/logs/<LogName>-stderr.log
```

Przy problemie runtime najważniejszy jest `*-serial.log`.

## Najczęstsze problemy

### System zatrzymuje się na splash/login

Sprawdź serial:

```powershell
Get-Content .\build\logs\<nazwa>-serial.log -Tail 160
```

Szukaj `userspace_init_spawn`, `userspace_init_pid1`, `desktop_session` i
`kurogane5_obsidian_login`.

### `cannot create /system/init as PID 1`

Upewnij się, że uruchamiasz Foundation GPT, a nie legacy `kurogane.img`.

### GUI działa bardzo wolno

Na Windows uruchom `run-qemu-fast.ps1` i sprawdź, czy aktywny jest WHPX. TCG
emuluje x86-64 programowo i jest szczególnie kosztowne dla software compositora.

### Aplikacja nie otwiera się

Użyj unikalnego `-LogName`, odtwórz problem i sprawdź serial. Blade raportuje
status uruchomienia procesu, a kernel loguje błędy WindowManager/session tree.

### WSL nie widzi repozytorium

```powershell
wsl.exe --shutdown
```

Następnie uruchom build ponownie. `scripts/wsl-path.ps1` obsługuje repozytoria
na dyskach innych niż `C:`.

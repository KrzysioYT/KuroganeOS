# KuroganeOS 3.3.3-dev — DEV BETA

KuroganeOS to eksperymentalny, 64-bitowy system operacyjny rozwijany od zera
dla **x86-64 + UEFI**. Nie jest dystrybucją Linuxa i nie używa kernela Linux.

> [!IMPORTANT]
> ## Pierwszy raz tutaj?
> **Nie musisz wiedzieć czym jest UEFI, GPT, AHCI ani cross-compiler.**
>
> Otwórz: **[docs/START_HERE.md](docs/START_HERE.md)**
>
> Ten poradnik prowadzi krok po kroku przez uruchomienie, VirtualBox, instalację,
> Windows, macOS, Linux i pierwszą aplikację.

`3.3.3-dev` jest wydaniem **DEV BETA**. Używaj go przede wszystkim w QEMU albo
VirtualBox i na pustych dyskach testowych.

---

## Chcę tylko uruchomić KuroganeOS

### VirtualBox — komputer Intel/AMD x86-64

Użyj:

```text
KuroganeOS-3.3.3-dev-x86_64.iso
```

Najważniejsze ustawienia VM:

```text
Firmware:       EFI / UEFI
Secure Boot:    OFF
RAM:            1024 MiB
CPU:            1-2
System disk:    SATA / Intel AHCI
Boot medium:    KuroganeOS ISO jako DVD
Boot order:     DVD -> Hard Disk
Network:        NAT
NIC:            Intel PRO/1000 MT Desktop (82540EM)
Audio:          Intel AC'97
Keyboard:       PS/2
Mouse:          PS/2
```

Pełna instrukcja: **[docs/VIRTUALBOX.md](docs/VIRTUALBOX.md)**.

Możesz też utworzyć referencyjną VM automatycznie:

Linux/macOS na obsługiwanym hoście x86-64:

```bash
bash ./scripts/create-virtualbox-vm.sh \
  --iso ./dist/KuroganeOS-3.3.3-dev-x86_64.iso
```

Windows:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\create-virtualbox-vm.ps1 `
  -Iso .\dist\KuroganeOS-3.3.3-dev-x86_64.iso
```

### Mac z Apple Silicon

KuroganeOS jest obecnie gościem x86-64, więc do developmentu na Apple Silicon
używaj **QEMU/TCG**.

Zobacz: [docs/MACOS_DEVELOPMENT.md](docs/MACOS_DEVELOPMENT.md).

---

## Co zobaczę po starcie ISO/IMG?

Nośnik 3.3.3-dev uruchamia Red Flux Setup:

```text
UEFI
  -> BOOTX64.EFI
  -> KuroganeOS kernel
  -> Red Flux Setup
       |
       +-- Try KuroganeOS
       |     -> read-only live root
       |     -> Login
       |     -> Red Flux Desktop
       |
       +-- Install KuroganeOS
             -> English / Polski
             -> username
             -> hasło / bez hasła
             -> wybór dysku
             -> wpisz INSTALL
             -> GPT + ESP + Kurogane Root
             -> verification
```

Po zalogowaniu Home działa jako **trwały root sesji**, ale nie zasłania pulpitu.
Dostęp do Home jest zawsze przez przypięty przycisk `HOME` w Docku oraz ikonę
`HOME` na pulpicie. Zamknięcie okna Home nie wylogowuje użytkownika — tylko je
chowa/minimalizuje.

3.3.3 dodaje również automatycznie uruchamianą aplikację **Performance**, która
jest przypięta do pulpitu i ustawiana po prawej stronie workspace.

> [!WARNING]
> Instalator kasuje wybrany dysk dopiero po wpisaniu dokładnego słowa
> `INSTALL`. Nadal używaj wyłącznie pustego VDI/obrazu/dysku testowego.

---

## VirtualBox ISO — ochrona przed `No bootable medium`

3.3.x używa czystego x86-64 UEFI ISO:

- dedykowany El Torito EFI entry;
- `EFI/BOOT/BOOTX64.EFI` — standardowa removable-media path;
- dedykowany obraz FAT16 30 MiB;
- `kernel.elf` i `install.pkg` wewnątrz obrazu EFI;
- prawidłowa GPT EFI System Partition;
- 20 niezależnych passów weryfikacji przed publikacją ISO;
- opcjonalny realny smoke boot przez Oracle VirtualBox;
- CI wykonujący dodatkowy boot optyczny przez OVMF/QEMU.

Builder **nie kopiuje ISO do `dist/`**, jeżeli obowiązkowy verifier zgłosi błąd.

Ręczna weryfikacja:

```bash
bash ./scripts/verify-virtualbox-iso.sh \
  ./dist/KuroganeOS-3.3.3-dev-x86_64.iso \
  --passes 20
```

Na Windows/x86-64 z zainstalowanym VirtualBox możesz wymusić realny smoke boot:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\build-media.ps1 `
  -Configuration release `
  -Rebuild `
  -VirtualBoxSmoke
```

---

## Budowanie KuroganeOS

### Windows 11 + WSL

> [!IMPORTANT]
> Windows wymaga dodatkowych plików toolchainu, których nie ma w Git.
>
> Pobierz:
> **[KuroganeOS — wymagane pliki build dla Windows](https://drive.google.com/file/d/1sHfNdDOOVeJh3Q0FOtUlqPbHZIZ-ykEk/view?usp=sharing)**
>
> Wypakuj zawartość do **głównego katalogu repozytorium**. Musi istnieć m.in.:
>
> ```text
> tools/compiler/x86_64-elf/bin/
> ```

Pełny IMG + ISO:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\build-media.ps1 `
  -Configuration release `
  -Rebuild
```

### macOS

Pierwszy raz:

```bash
chmod +x scripts/*.sh
bash ./scripts/setup-macos.sh --install
```

Build IMG + ISO:

```bash
bash ./scripts/build-media-macos.sh \
  --configuration release \
  --rebuild
```

### Linux x86-64

Pierwszy raz:

```bash
chmod +x scripts/*.sh
bash ./scripts/setup-linux.sh --install
```

Build IMG + ISO:

```bash
bash ./scripts/build-media-linux.sh \
  --configuration release \
  --rebuild
```

### Wyniki

```text
dist/KuroganeOS-3.3.3-dev-windows-qemu.img
dist/KuroganeOS-3.3.3-dev-macos-qemu.img
dist/KuroganeOS-3.3.3-dev-linux-qemu.img
dist/KuroganeOS-3.3.3-dev-x86_64.iso
dist/SHA256SUMS.txt
```

Host tworzy tylko swój wariant IMG. ISO ma wspólną nazwę.

---

## Internet i Kurogane Web

Referencyjna karta sieciowa VM to:

```text
Intel PRO/1000 MT Desktop / 82540EM / PCI 8086:100E
```

KuroganeOS ma własny sterownik E1000 i stos:

```text
Ethernet
ARP
IPv4
ICMP
UDP
DHCP
DNS A
TCP
HTTP/1.0 GET
```

3.3.3 dodaje pierwszą aplikację **Kurogane Web** połączoną z tym stosem przez
publiczny, ograniczony Ring-3 HTTP ABI. Przykład:

```text
http://example.com/
```

Kurogane Web potrafi pobrać odpowiedź z Internetu i wyświetlić prosty tekst z
HTML. W DEV BETA obowiązują ograniczenia: tylko HTTP/port 80, maks. 4096 B na
żądanie, prosty renderer i brak TLS/HTTPS.

**To nie jest jeszcze Chromium.** Port Chromium wymaga m.in. asynchronicznych
socketów, TLS, szerszego libc/POSIX, wątków, timerów, filesystem/process API i
integracji sandboxa. Nie oznaczamy takiego backendu jako gotowego zanim realnie
nie istnieje.

W VirtualBox ustaw:

```text
Attached to: NAT
Adapter Type: Intel PRO/1000 MT Desktop (82540EM)
Cable Connected: ON
```

Jeżeli DHCP/NAT jest chwilowo niedostępne, system przechodzi do loopback zamiast
przerywać start.

Więcej: [docs/NETWORKING.md](docs/NETWORKING.md).

---

## Dźwięk

3.3.x zawiera bazowy kernelowy driver:

```text
Intel ICH AC'97 / PCI 8086:2415
```

Referencyjny format PCM:

```text
48000 Hz
stereo
signed 16-bit little-endian
```

W VirtualBox ustaw `Audio: ON`, `Controller: Intel AC'97`, `Audio Output: ON`.
Publiczny userspace streaming API pozostaje przyszłą warstwą.

Więcej: [docs/AUDIO.md](docs/AUDIO.md).

---

## GPU i DirectX 9/11/12 — status

3.3.3 dodaje driver capabilities dla urządzeń klasy PCI Display. Kernel
rozróżnia teraz:

```text
PCI display adapter
UEFI GOP scanout
Red Flux software compositor
hardware accelerated 3D
```

Na obecnym backendzie `hardware accelerated 3D` pozostaje wyłączone. Aktualna
wartość GPU/GFX w Performance mierzy aktywność compositora/GOP, a nie fizyczne
rdzenie GPU.

KuroganeOS **nie oznacza jeszcze Direct3D 9, 11 ani 12 jako gotowych**. Pełna
zgodność wymaga zasobów GPU, shaderów, command submission, synchronizacji,
presentation oraz rzeczywistego software/hardware backendu. Nie dodajemy atrap
`D3D12CreateDevice()` zwracających sukces bez implementacji.

Dokładny plan: **[docs/GRAPHICS_COMPATIBILITY.md](docs/GRAPHICS_COMPATIBILITY.md)**.

---

## Chcę napisać program dla KuroganeOS

Start: **[docs/DEVELOPERS/README.md](docs/DEVELOPERS/README.md)**

- [APP_DEVELOPMENT.md](docs/DEVELOPERS/APP_DEVELOPMENT.md) — pierwszy program;
- [GUI_APPLICATIONS.md](docs/DEVELOPERS/GUI_APPLICATIONS.md) — okna i libui;
- [API_REFERENCE.md](docs/DEVELOPERS/API_REFERENCE.md) — publiczne API;
- [KERNEL_CONTRIBUTION.md](docs/DEVELOPERS/KERNEL_CONTRIBUTION.md) — kernel i sterowniki.

Aplikacje są ELF64 x86-64 Ring-3, freestanding/static i używają KuroganeOS
syscall ABI. Nie są to programy Windows `.exe` ani binaria Linux.

---

## Red Flux Desktop

Aktualny desktop zawiera m.in.:

- boot splash i Try/Install Setup;
- Login;
- trwały Red Flux Home jako session root;
- przypięty przycisk Home w Docku;
- skróty aplikacji na pulpicie;
- przypinanie/odpinanie aplikacji przez Home (`P`);
- Performance autostart z live CPU/GPU-GFX/RAM/disk;
- Kurogane Web;
- Terminal, Files, System Monitor, Settings i About;
- focus/z-order, drag, resize, minimize/maximize/restore/close;
- software backbuffer i damage-style GOP scanout.

Sterowanie:

```text
Mouse             focus / drag / resize / Dock / desktop shortcuts
Arrow keys        nawigacja
Enter             zatwierdzenie
P w Home          pin/unpin zaznaczonej aplikacji
Escape            anulowanie
Tab               następny element
Alt+Tab           zmiana okna
Alt+F4            zamknięcie zwykłego okna; Home jest tylko chowane
```

---

## Dokumentacja — mapa

### Zwykły użytkownik

- **[START_HERE.md](docs/START_HERE.md)**
- [RUNNING.md](docs/RUNNING.md)
- [VIRTUALBOX.md](docs/VIRTUALBOX.md)
- [INSTALLATION.md](docs/INSTALLATION.md)
- [NETWORKING.md](docs/NETWORKING.md)
- [AUDIO.md](docs/AUDIO.md)

### Programista aplikacji

- **[DEVELOPERS/README.md](docs/DEVELOPERS/README.md)**
- [DEVELOPERS/APP_DEVELOPMENT.md](docs/DEVELOPERS/APP_DEVELOPMENT.md)
- [DEVELOPERS/GUI_APPLICATIONS.md](docs/DEVELOPERS/GUI_APPLICATIONS.md)
- [DEVELOPERS/API_REFERENCE.md](docs/DEVELOPERS/API_REFERENCE.md)

### Programista systemu

- [DEVELOPERS/KERNEL_CONTRIBUTION.md](docs/DEVELOPERS/KERNEL_CONTRIBUTION.md)
- [ARCHITECTURE.md](docs/ARCHITECTURE.md)
- [DRIVERS.md](docs/DRIVERS.md)
- [FILESYSTEM.md](docs/FILESYSTEM.md)
- [GUI.md](docs/GUI.md)
- [BOOT_PROCESS.md](docs/BOOT_PROCESS.md)
- [GRAPHICS_COMPATIBILITY.md](docs/GRAPHICS_COMPATIBILITY.md)

### Status projektu

- [BUILD_STATUS.md](docs/BUILD_STATUS.md)
- [CURRENT_LIMITATIONS.md](docs/CURRENT_LIMITATIONS.md)
- [roadmap/DESKTOP_ROADMAP.md](docs/roadmap/DESKTOP_ROADMAP.md)
- [releases/3.3.3-dev.md](docs/releases/3.3.3-dev.md)

---

## Licencja

Aktualne rewizje KuroganeOS są udostępniane na warunkach
**KuroganeOS Source-Available License 1.0 (KSAL-1.0)**.

Nie jest to licencja Open Source zatwierdzona przez OSI.

- [LICENSE](LICENSE)
- [LICENSE-MIT-LEGACY](LICENSE-MIT-LEGACY)
- [docs/LICENSING.md](docs/LICENSING.md)
- [CLA.md](CLA.md)
- [CONTRIBUTING.md](CONTRIBUTING.md)

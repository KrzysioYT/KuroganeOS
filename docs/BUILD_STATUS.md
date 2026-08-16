# Build status

Data: 17 sierpnia 2026 r.

## Current stage

KuroganeOS **3.3.1-dev — DEV BETA VirtualBox Qualification & Enablement** jest
aktualną linią rozwojową. 3.3.1 wzmacnia nośnik UEFI/ISO, dodaje referencyjny
profil VirtualBox, kernelowy backend Intel ICH AC'97, stabilniejszy fallback
sieciowy oraz beginner/developer documentation.

## Working foundation

- UEFI `BOOTX64.EFI`, boot protocol v3;
- VMM, GDT/TSS/IST, IDT;
- Ring 3, `int 0x80`, ELF64, PID/TID;
- process spawn/wait/exit i PIT preemption;
- `/system/init` jako PID 1;
- AHCI, GPT, writable FAT32/VFS, persistent root;
- PS/2 keyboard/mouse, PCI, ACPI/APIC;
- WindowManager + Red Flux Dock;
- software backbuffer, clipping i damage-style GOP scanout;
- Ring-3 `libui` scene/view runtime;
- wspólny `FluxShellCore`;
- rzeczywisty kernelowy installer GPT/FAT32;
- read-only package-backed VFS dla live media;
- Intel E1000/82540EM `8086:100E`;
- Intel ICH AC'97 `8086:2415` kernel PCM backend.

## 3.3.1 DEV BETA changes

- wersja `3.3.1-dev`, kanał `DEV BETA`;
- beginner-first `docs/START_HERE.md`;
- osobna kompletna instrukcja `docs/VIRTUALBOX.md`;
- developer documentation w `docs/DEVELOPERS/`;
- referencyjny profil VirtualBox: EFI64, SATA/AHCI, E1000 82540EM, NAT, AC'97;
- helpery tworzące referencyjną VM na Windows i Unix-like hostach;
- naprawa nośnika El Torito: historyczny obraz 64 MiB został zastąpiony
  obrazem FAT16 30 MiB / 61440 sektorów po 512 B;
- El Torito platform EFI + removable-media path `EFI/BOOT/BOOTX64.EFI`;
- ten sam EFI boot image jest wystawiany w GPT jako EFI System Partition;
- builder ISO ma mandatory publication gate — 20 niezależnych passów;
- osobny OVMF/QEMU optical smoke boot do rzeczywistego markera kernela;
- Windows media build może wymusić realny Oracle VirtualBox boot przez
  `-VirtualBoxSmoke`;
- E1000 pozostaje referencyjnym NIC dla VirtualBox NAT;
- brak DHCP/linku nie zatrzymuje już całego desktop boot — kernel publikuje
  loopback fallback i zachowuje stan błędu fizycznego interfejsu;
- dodano Intel ICH AC'97 kernel driver: PCM S16LE stereo 48 kHz, bus-master DMA32;
- AC'97 rejestruje się przez centralny driver manager;
- publiczny network/audio Ring-3 ABI nie został zamrożony jako blocking syscall;
  docelowy model pozostaje asynchronicznym handle/event service;
- dokumentacja grafiki opisuje prawdziwą ścieżkę do przyszłej zgodności D3D,
  bez fałszywego oznaczania DirectX 11/12 jako gotowego.

## Automated qualification — latest code revision

Automatyczny workflow x86-64/Linux dla rewizji zawierającej aktualny kod
ISO/network/audio zakończył się sukcesem.

```text
full Linux media build:                         PASS
installer ESP size: 30 MiB / 61440 sectors:   PASS
mandatory builder ISO verification:            20/20 PASS
workflow second ISO verification:              20/20 PASS
OVMF/QEMU optical UEFI boot -> kernel marker: PASS
qualification artifact upload:                 PASS
```

Oznacza to **40 pełnych strukturalnych inspekcji tego samego modelu ISO w jednym
workflow plus rzeczywisty boot optyczny przez niezależny firmware OVMF/QEMU**.

To jest mocny dowód poprawności nośnika UEFI, ale nie jest zastępowane
marketingowym stwierdzeniem „100% na każdej konfiguracji VirtualBox”.

## Oracle VirtualBox qualification

Realny smoke test Oracle VirtualBox jest gotowy i stanowi ostatni host-specific
gate dla deklaracji `VirtualBox runtime PASS`:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\build-media.ps1 `
  -Configuration release `
  -Rebuild `
  -VirtualBoxSmoke
```

Oczekiwane markery:

```text
[virtualbox-smoke] EFI optical boot: PASS
[virtualbox-smoke] BOOTX64.EFI -> kernel serial marker: PASS
[virtualbox-smoke] VIRTUALBOX REAL BOOT VERIFIED
```

Dopóki ten test nie zostanie wykonany na hoście x86-64 z Oracle VirtualBox,
status brzmi:

```text
UEFI ISO automated qualification: PASS
Oracle VirtualBox real smoke: AVAILABLE / NOT YET RECORDED
```

## Zalecane build commands

### Windows + WSL

Wymagane dodatkowe pliki:

https://drive.google.com/file/d/1sHfNdDOOVeJh3Q0FOtUlqPbHZIZ-ykEk/view?usp=sharing

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\build-media.ps1 `
  -Configuration release `
  -Rebuild
```

### macOS

```bash
bash ./scripts/setup-macos.sh --install
bash ./scripts/build-media-macos.sh --configuration release --rebuild
```

### Linux x86-64

```bash
bash ./scripts/setup-linux.sh --install
bash ./scripts/build-media-linux.sh --configuration release --rebuild
```

## Expected artifacts

```text
dist/KuroganeOS-3.3.1-dev-windows-qemu.img
dist/KuroganeOS-3.3.1-dev-macos-qemu.img
dist/KuroganeOS-3.3.1-dev-linux-qemu.img
dist/KuroganeOS-3.3.1-dev-x86_64.iso
dist/SHA256SUMS.txt
```

Jeden host tworzy swój host-specific IMG oraz wspólny ISO.

## Runtime acceptance still open

1. Oracle VirtualBox x86-64: ISO optical boot -> kernel marker;
2. VirtualBox: ISO -> Try -> Login -> Home;
3. VirtualBox: Install -> target SATA VDI -> reboot without ISO -> Login;
4. VirtualBox NAT + 82540EM -> DHCP/gateway/DNS online path;
5. VirtualBox AC'97 -> audible PCM smoke;
6. live root pozostaje read-only;
7. EN install bez hasła -> reboot -> Login -> Home;
8. PL install z hasłem -> błędne hasło odrzucone, poprawne zaakceptowane;
9. brak zapisu na target przed poprawnym `INSTALL`;
10. System Monitor pozostaje bez full-screen flickera.

## Known gaps / DEV warnings

- `FNV1A64-DEV` nie jest bezpiecznym password KDF;
- brak pełnej account service/credential store/lock screen;
- publiczne userspace sockets/DNS/ping są nadal planowanym async API;
- AC'97 ma realny kernel backend, ale stabilne publiczne Ring-3 audio stream API
  nadal jest planowane;
- pełny Direct3D/DirectX 9/10/11/12 nie jest jeszcze zaimplementowany;
- brak pełnego native graphics runtime/GPU acceleration;
- compatibility `ku_ui_frame` pozostaje transportem części aplikacji;
- brak kompletnego publicznego Ring-3 file capability API;
- real-hardware qualification pozostaje węższa niż VM qualification.

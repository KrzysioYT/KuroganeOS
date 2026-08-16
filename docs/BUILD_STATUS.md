# Build status

Data: 16 sierpnia 2026 r.

## Current stage

KuroganeOS **3.3.0-dev — DEV BETA Media & Installer** jest aktualną linią
rozwojową. 3.3 rozszerza Red Flux Desktop Shell o wspólny model nośnika
`Try / Install`, live root, profil językowy/konta oraz natywne workflow build na
Windows/WSL, macOS i Linux x86-64.

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
- read-only package-backed VFS dla live media.

## 3.3 DEV BETA changes

- wersja `3.3.0-dev`, kanał `DEV BETA`;
- IMG i ISO mają wspólny entry flow `Try KuroganeOS / Install KuroganeOS`;
- `install.pkg` może działać jako read-only live root;
- Try prowadzi do zwykłego Login/Home bez zapisu na dysk;
- Installer ma Red Flux setup UI zamiast starego tekstowego promptu;
- wybór `English` / `Polski`;
- lokalna nazwa użytkownika;
- konto bez hasła albo z hasłem;
- graficzny wybór dysku SATA/AHCI;
- dokładne `INSTALL` nadal chroni pierwszy destrukcyjny zapis;
- profil instalacji zapisuje `/etc/locale.cfg` i `/etc/user.cfg`;
- Login czyta zainstalowany profil i opcjonalnie weryfikuje hasło;
- `FNV1A64-DEV` jest jawnie tymczasowym verifierem DEV BETA, nie produkcyjnym KDF;
- ujednolicone media buildy tworzą IMG + ISO;
- natywny Linux x86-64 build frontend i setup zależności;
- macOS i Windows/WSL pozostają wspierane.

## Zalecane build commands

### Windows + WSL

Wymagane dodatkowe pliki:

https://drive.google.com/file/d/1sHfNdDOOVeJh3Q0FOtUlqPbHZIZ-ykEk/view?usp=sharing

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-media.ps1 -Configuration release -Rebuild
```

### macOS

```bash
./scripts/setup-macos.sh --install
bash ./scripts/build-media-macos.sh --configuration release --rebuild
```

### Linux x86-64

```bash
bash ./scripts/setup-linux.sh --install
bash ./scripts/build-media-linux.sh --configuration release --rebuild
```

## Expected artifacts

```text
dist/KuroganeOS-3.3.0-dev-windows-qemu.img
dist/KuroganeOS-3.3.0-dev-macos-qemu.img
dist/KuroganeOS-3.3.0-dev-linux-qemu.img
dist/KuroganeOS-3.3.0-dev-x86_64.iso
dist/SHA256SUMS.txt
```

Jeden host tworzy swój host-specific IMG oraz wspólny ISO.

## Validation state

3.3-dev **nie jest jeszcze runtime-verified**. Zmiany wymagają świeżego pełnego
build/test po tej rewizji. Minimalne acceptance:

1. IMG -> Try -> Login -> Home;
2. ISO -> Try -> Login -> Home;
3. live root pozostaje read-only;
4. EN install bez hasła -> reboot z target HDD -> Login -> Home;
5. PL install z hasłem -> reboot -> błędne hasło odrzucone, poprawne zaakceptowane;
6. brak zapisu na target przed poprawnym `INSTALL`;
7. GPT/ESP/root i package verification PASS;
8. System Monitor nie powoduje full-screen flickera;
9. media build kończy się poprawnie na Windows, macOS i Linux.

## Known gaps / DEV warnings

- `FNV1A64-DEV` nie jest bezpiecznym password KDF;
- brak pełnej account service/credential store/lock screen;
- live package VFS jest read-only i nie ma jeszcze pełnego `readdir`;
- compatibility `ku_ui_frame` pozostaje transportem części aplikacji;
- brak pełnego native widget ABI/per-window compositor;
- brak kompletnego publicznego Ring-3 file capability API;
- Linux support jest nową ścieżką 3.3 i wymaga runtime/build acceptance na realnym hoście;
- ISO oraz instalacja na realnym sprzęcie nie są jeszcze oznaczone jako stabilne.

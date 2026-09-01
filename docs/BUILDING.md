# Budowanie KuroganeOS

Aktualny publiczny numer development builda to `3.3.3-dev`. Gałąź GUI rozwija
Forged Steel/KuroganeOS 5, ale numer wydania nie jest podnoszony do 5.0.0 przed
pełnym Definition of Done i kwalifikacją release.

## Windows + WSL2

Kanoniczny build kernela/userspace na Windows używa repozytoryjnego toolchainu:

```text
tools/compiler/x86_64-elf/bin/
```

WSL2 dostarcza Bash/Python oraz narzędzia filesystem/media. Minimalny zestaw dla
pełnej kwalifikacji obejmuje m.in. `bash`, `python3`, `g++`, `make`, `fsck.fat`,
`sgdisk`, `xorriso`, `base64` i `wslpath`.

## Główny frontend builda

```powershell
.\scripts\build.ps1 -Configuration debug
.\scripts\build.ps1 -Configuration release
.\scripts\build.ps1 -Configuration test
```

Pełny rebuild:

```powershell
.\scripts\build.ps1 -Configuration debug -Rebuild
```

`build.ps1` buduje nie tylko kernel. Pipeline obejmuje aktualnie m.in.:

```text
kernel
Ring-3 userspace
SDK/libc/libui
BOOTX64.EFI
rootfs overlay
legacy FAT artifact
Foundation GPT image
install.pkg
installer staging/media
```

## Profile

| Profil | Zastosowanie |
|---|---|
| `debug` | `-O0`, symbole i rozbudowana diagnostyka |
| `test` | runtime/qualification probes |
| `release` | optymalizowany build do media pipeline |

Kernel pozostaje freestanding C++17 bez exceptions/RTTI/red-zone. Userspace jest
ELF64 x86-64 Ring-3 i używa własnego KuroganeOS syscall ABI.

## Artefakty developerskie

Po `build.ps1` najważniejsze są:

```text
build/kernel.elf
build/kernel.map
build/BOOTX64.EFI
build/build-info.txt
kurogane.img
build/images/KuroganeOS-base.img
state/KuroganeOS.img          # jeżeli working image został utworzony
build/install.pkg
```

Znaczenie obrazów:

- `kurogane.img` — legacy 64 MiB FAT/EFI artifact; nie jest pełnym userspace
  disk;
- `build/images/KuroganeOS-base.img` — deterministyczny Foundation GPT z ESP i
  `Kurogane Root`; właściwy do testów PID1/login/desktop;
- `state/KuroganeOS.img` — zachowywany working image, którego partycja root może
  zawierać dane z wcześniejszych sesji.

`-Rebuild` nie powinien bez pytania niszczyć zachowywanego working root. Pełny
reset working image jest operacją jawną:

```powershell
.\scripts\build-foundation-image.ps1 -ResetWorkingImage
```

## Media Windows

```powershell
.\scripts\build-media.ps1 -Configuration release -Rebuild
```

Bieżący kontrakt nazw:

```text
dist/KuroganeOS-3.3.3-dev-qemu-x86_64.img
dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
dist/SHA256SUMS.txt
```

Builder Windows usuwa stare niejednoznaczne aliasy `*-windows-qemu.img` oraz
`*-x86_64.iso` dla bieżącej wersji. QEMU IMG i VirtualBox ISO mają różne
kontrakty i nazwy celowo.

`build-media.ps1` eksportuje również systemowy Windows trust store do rootfs
Kurogane Web i przebudowuje warstwy, które go konsumują.

## macOS

```bash
chmod +x scripts/*.sh
bash ./scripts/setup-macos.sh --install
bash ./scripts/build-media-macos.sh --configuration release --rebuild
```

Development-only:

```bash
bash ./scripts/build-macos.sh --configuration debug --rebuild
```

Apple Silicon pozostaje hostem, target KuroganeOS jest nadal x86-64.

## Linux

```bash
chmod +x scripts/*.sh
bash ./scripts/setup-linux.sh --install
bash ./scripts/build-media-linux.sh --configuration release --rebuild
```

## Anvil repository configuration

Installer/rootfs używa FAT 8.3-safe ścieżki:

```text
/etc/anvil.cfg
```

Nie przywracaj `/etc/anvil.repo`; czteroznakowe rozszerzenie `.repo` łamie
kontrakt installera FAT 8.3.

## Build lock

Windows build używa:

```text
state/.build.lock
```

Druga równoległa instancja ma zakończyć się błędem zamiast modyfikować te same
artefakty.

## Po buildzie

Sam poprawny linker exit code nie oznacza, że system bootuje. Minimalny kolejny
krok:

```powershell
wsl.exe --exec bash -lc "cd /mnt/e/KuroganeOS && bash ./scripts/test.sh"
```

Następnie Foundation QEMU test albo pełne:

```powershell
.\scripts\verify.ps1 -TimeoutSeconds 90 -KeepLogs
```

Szczegóły: [TESTING.md](TESTING.md) i [RUNNING.md](RUNNING.md).

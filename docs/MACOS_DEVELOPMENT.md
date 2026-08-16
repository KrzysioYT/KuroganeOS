# KuroganeOS 2.2.5 — rozwój na macOS

KuroganeOS 2.2.5 posiada natywny workflow macOS dla kernela, Ring-3 userspace,
SDK, Kurogane Flux Desktop, obrazów QEMU oraz **instalowalnych ISO UEFI**.
Nie wymaga WSL ani Windows PowerShell. Host może być Apple Silicon albo Intel;
target KuroganeOS pozostaje `x86_64`.

## 1. Przygotowanie środowiska

```bash
chmod +x scripts/*.sh
./scripts/setup-macos.sh --install
```

Skrypt instaluje/sprawdza m.in.:

```text
x86_64-elf-binutils
x86_64-elf-gcc
qemu
mtools
dosfstools
gptfdisk
xorriso
python
```

Samo sprawdzenie:

```bash
./scripts/setup-macos.sh
```

`xorriso` jest od 2.2.5 obowiązkowym elementem macOS toolchaina, ponieważ
builder ISO działa natywnie na Macu.

## 2. Zwykły build development IMG

Pełny debug rebuild:

```bash
./scripts/build-macos.sh --configuration debug --rebuild
```

Release IMG:

```bash
./scripts/build-macos.sh --configuration release --rebuild
```

Wyniki:

```text
build/kernel.elf
build/BOOTX64.EFI
build/sdk/sysroot/
build/userspace/rootfs/
build/images/KuroganeOS-macos.img
dist/KuroganeOS-2.2.5-macos-qemu.img
```

## 3. Build instalowalnego ISO na macOS

### Najprostsza komenda

Buduje IMG development **i** instalowalne ISO:

```bash
./scripts/build-macos.sh --configuration release --rebuild --iso
```

ISO:

```text
dist/KuroganeOS-2.2.5-x86_64.iso
```

Checksum:

```text
dist/SHA256SUMS.txt
```

Compatibility copy:

```text
kurogane.iso
```

### Tylko installer/ISO

Jeżeli nie potrzebujesz development IMG:

```bash
./scripts/build-installer-macos.sh --configuration release --rebuild
```

Builder kompiluje system przez natywny backend macOS, tworzy `install.pkg`,
buduje osobny 64 MiB FAT32 EFI System Partition i następnie generuje właściwe
UEFI/El Torito ISO przez `xorriso`. Nie wykonuje konwersji `.img -> .iso`.

### Stara komenda zgodności

Od 2.2.5 również to działa natywnie na macOS:

```bash
./scripts/build-iso.sh release
```

Na Darwin skrypt automatycznie przechodzi do `build-installer-macos.sh` zamiast
szukać PowerShella.

### Reuse istniejącego builda

Jeżeli `build/kernel.elf`, `build/BOOTX64.EFI` oraz
`build/userspace/rootfs/` są aktualne:

```bash
./scripts/build-installer-macos.sh --configuration release --no-build
```

## 4. Naprawa błędu `version.h`

2.2.0 mogło zatrzymać macOS SDK build na:

```text
userspace/gui/terminal/main.c: fatal error: version.h: No such file or directory
```

Przyczyną było to, że GUI SDK compile path nie widział katalogu `common/`.
2.2.5 naprawia zarówno ścieżkę include buildera SDK, jak i include Terminala,
więc `KUROGANE_VERSION_STRING` nie zależy już od przypadkowego working directory.

## 5. QEMU

Automatyczny smoke test:

```bash
./scripts/run-qemu-macos.sh
```

Desktop Developer Preview:

```bash
./scripts/run-qemu-macos.sh --display
```

Konkretny obraz:

```bash
./scripts/run-qemu-macos.sh \
  --image ./dist/KuroganeOS-2.2.5-macos-qemu.img \
  --display
```

Apple Silicon uruchamia gościa x86-64 przez QEMU TCG.

## 6. Własna aplikacja

C:

```bash
./scripts/build-app-macos.sh moja-aplikacja.c -o moja-aplikacja --install
```

C++:

```bash
./scripts/build-app-macos.sh moja-aplikacja.cpp -o moja-aplikacja --install
```

Odśwież development image:

```bash
./scripts/build-macos.sh --configuration debug --stage-only
```

Następnie w Flux Console:

```text
run moja-aplikacja
```

## 7. Makefile

```bash
make CONFIG=debug
make CONFIG=release
make kernel CONFIG=debug
make verify CONFIG=debug
make clean
```

## 8. Co buduje macOS

Windows i macOS wytwarzają ten sam target KuroganeOS:

- ELF64 kernel x86-64;
- PE32+ AMD64 `BOOTX64.EFI`;
- ELF64 ET_EXEC Ring-3 apps;
- publiczny SDK ABI;
- GPT/FAT32 development IMG;
- instalowalny UEFI ISO.

macOS jest hostem developerskim, a nie osobnym systemowym targetem.

## 9. Weryfikacja

Skrypty 2.2.5 przechodzą kontrolę składni Bash w repozytorium. Pełny runtime
PASS wymaga wykonania na rzeczywistym Macu:

```bash
./scripts/setup-macos.sh
./scripts/build-macos.sh --configuration debug --rebuild --iso
./scripts/run-qemu-macos.sh --display
```

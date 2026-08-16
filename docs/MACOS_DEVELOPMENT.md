# KuroganeOS 2.2 — rozwój na macOS

Natywny workflow macOS wprowadzony w 2.1.1 pozostaje wspieranym backendem w
KuroganeOS 2.2. Nie wymaga WSL ani Windows PowerShell. Host może być Apple
Silicon albo Intel; artefakty KuroganeOS nadal są budowane dla `x86_64-elf`.

## Przygotowanie środowiska

```bash
chmod +x scripts/*.sh
./scripts/setup-macos.sh --install
```

Instalowane są m.in. `x86_64-elf-gcc/binutils`, QEMU, mtools, dosfstools,
gptfdisk, xorriso i Python.

Sprawdzenie bez instalowania:

```bash
./scripts/setup-macos.sh
```

## Pełny build

Debug:

```bash
./scripts/build-macos.sh --configuration debug
```

Release:

```bash
./scripts/build-macos.sh --configuration release
```

Pełny rebuild:

```bash
./scripts/build-macos.sh --configuration debug --rebuild
```

Build tworzy kernel, Ring-3 userspace, SDK, GUI, `BOOTX64.EFI` oraz 512 MiB GPT
image z ESP FAT32 i persistent root FAT32.

Najważniejsze wyniki dla 2.2:

```text
build/kernel.elf
build/BOOTX64.EFI
build/sdk/sysroot/
build/userspace/rootfs/
build/images/KuroganeOS-macos.img
dist/KuroganeOS-2.2.0-macos-qemu.img
```

## Własna aplikacja

C:

```bash
./scripts/build-app-macos.sh moja-aplikacja.c -o moja-aplikacja
```

C++:

```bash
./scripts/build-app-macos.sh moja-aplikacja.cpp -o moja-aplikacja
```

Instalacja do kolejnych development buildów:

```bash
./scripts/build-app-macos.sh moja-aplikacja.c -o moja-aplikacja --install
./scripts/build-macos.sh --configuration debug --stage-only
```

Program jest przechowywany w:

```text
state/macos-apps/moja-aplikacja
```

Po starcie 2.2 można użyć krótszej składni Flux Console:

```text
run moja-aplikacja
```

lub dokładnej ścieżki:

```text
run /apps/moja-aplikacja
```

## QEMU

Smoke test:

```bash
./scripts/run-qemu-macos.sh
```

Okno graficzne / Desktop Developer Preview:

```bash
./scripts/run-qemu-macos.sh --display
```

Inny obraz:

```bash
./scripts/run-qemu-macos.sh --image ./dist/KuroganeOS-2.2.0-macos-qemu.img --display
```

Runner wykrywa EDK2 z Homebrew QEMU, używa q35, development GPT image, E1000 i
logu szeregowego. Apple Silicon emuluje x86-64 przez TCG.

## Makefile

```bash
make CONFIG=debug
make CONFIG=release
make kernel CONFIG=debug
make verify CONFIG=debug
make clean
```

Darwin używa `x86_64-elf-*` z `PATH`; Windows zachowuje istniejący backend
PowerShell i repozytoryjny toolchain.

## Wspólne ABI

Windows i macOS budują ten sam:

- ELF64 kernel x86-64;
- PE32+ AMD64 UEFI loader;
- ELF64 ET_EXEC Ring-3 apps;
- publiczny SDK ABI;
- GPT/FAT32 development image.

macOS jest hostem developerskim, a nie osobnym targetem KuroganeOS.

## Weryfikacja

Sam commit nie jest dowodem wykonania Homebrew toolchaina. Akceptacja na Macu
wymaga rzeczywistego `setup-macos.sh`, builda i QEMU smoke-testu.

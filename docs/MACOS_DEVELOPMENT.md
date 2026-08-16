# KuroganeOS 2.1.1 — rozwój na macOS

KuroganeOS 2.1.1 dodaje natywny workflow deweloperski dla macOS. Nie wymaga WSL ani Windows PowerShell. Host może być Apple Silicon albo Intel; wszystkie artefakty KuroganeOS nadal są budowane dla `x86_64-elf` i uruchamiane przez QEMU.

## 1. Przygotowanie środowiska

Wymagany jest Homebrew. Repozytorium zawiera checker/installer zależności:

```bash
chmod +x scripts/*.sh
./scripts/setup-macos.sh --install
```

Instalowane są między innymi:

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

Samo sprawdzenie środowiska bez instalowania pakietów:

```bash
./scripts/setup-macos.sh
```

## 2. Pełny build systemu

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

Build tworzy kolejno:

1. kernel x86-64;
2. podstawowe aplikacje Ring 3;
3. SDK (`crt0`, `libc`, `libkurogane`, `libui`);
4. aplikację zewnętrzną i GUI userspace;
5. własny `BOOTX64.EFI`;
6. staging UEFI;
7. 512 MiB GPT image z ESP FAT32 i persistent root FAT32.

Najważniejsze wyniki:

```text
build/kernel.elf
build/BOOTX64.EFI
build/sdk/sysroot/
build/userspace/rootfs/
build/images/KuroganeOS-macos.img
dist/KuroganeOS-2.1.1-macos-qemu.img
```

## 3. Budowanie własnej aplikacji

Dla aplikacji C:

```bash
./scripts/build-app-macos.sh moja-aplikacja.c -o moja-aplikacja
```

Dla C++:

```bash
./scripts/build-app-macos.sh moja-aplikacja.cpp -o moja-aplikacja
```

Wynik:

```text
build/apps/moja-aplikacja
```

Aby dodać program do kolejnych development buildów jako `/apps/moja-aplikacja`:

```bash
./scripts/build-app-macos.sh moja-aplikacja.c -o moja-aplikacja --install
```

`--install` zapisuje ELF do:

```text
state/macos-apps/moja-aplikacja
```

`state/` jest ignorowany przez Git i nie jest usuwany przez zwykły `--clean`/`--rebuild`. Następnie odśwież obraz:

```bash
./scripts/build-macos.sh --configuration debug --stage-only
```

Po starcie KuroganeOS uruchom program w userspace shell:

```text
run /apps/moja-aplikacja
```

Aplikacja jest linkowana z publicznym SDK KuroganeOS i przechodzi kontrolę ET_EXEC oraz W^X.

## 4. Test w QEMU

Automatyczny smoke test:

```bash
./scripts/run-qemu-macos.sh
```

Runner:

- znajduje firmware EDK2 dostarczony z Homebrew QEMU;
- uruchamia `qemu-system-x86_64` na maszynie `q35`;
- na Apple Silicon używa pełnej emulacji x86-64 przez TCG;
- podłącza development GPT image jako dysk systemowy;
- dodaje E1000 z QEMU user networking;
- zapisuje serial do `build/logs/qemu-macos-serial.log`;
- kończy sukcesem dopiero po `userspace_init_spawn: PASS` i `ALL_REQUIRED_TESTS_PASSED`;
- przerywa przy wymaganym `FAIL`, panic lub `fatal:`.

Okno graficzne:

```bash
./scripts/run-qemu-macos.sh --display
```

Inny obraz:

```bash
./scripts/run-qemu-macos.sh --image ./dist/KuroganeOS-2.1.1-macos-qemu.img
```

## 5. Makefile

Na macOS można też użyć:

```bash
make CONFIG=debug
make CONFIG=release
make kernel CONFIG=debug
make verify CONFIG=debug
make clean
```

`make` automatycznie rozpoznaje Darwin i używa narzędzi `x86_64-elf-*` z `PATH`. Windows nadal korzysta z dotychczasowego PowerShell backendu i repozytoryjnego toolchaina `.exe`.

## 6. Różnice Apple Silicon / Intel

KuroganeOS pozostaje systemem x86-64. Na Intel Mac QEMU może wykonywać go na tej samej architekturze hosta. Na Apple Silicon QEMU wykonuje emulację x86-64; jest to wolniejsze od natywnej wirtualizacji ARM, ale zachowuje właściwą architekturę gościa i pozwala testować ten sam kernel/ABI co na PC.

## 7. Co jest wspólne z Windows

macOS nie ma osobnego formatu kernela ani aplikacji. Oba hosty budują:

- ELF64 x86-64 kernel;
- PE32+ AMD64 UEFI application;
- ELF64 ET_EXEC Ring-3 applications;
- ten sam publiczny KuroganeOS SDK ABI;
- GPT + FAT32 development image.

Dzięki temu kod aplikacji napisany na Macu nie jest „wersją macOS” programu — jest normalną aplikacją KuroganeOS skompilowaną cross-toolchainem na hoście macOS.

## 8. Ograniczenie weryfikacji wydania

Skrypty 2.1.1 zostały przygotowane tak, aby nie zależały od PowerShell/WSL w ścieżce macOS. Finalną akceptację runtime należy wykonać na rzeczywistym macOS przez `setup-macos.sh`, `build-macos.sh` i `run-qemu-macos.sh`, ponieważ sam commit w repozytorium nie jest dowodem wykonania toolchaina Homebrew na Macu.

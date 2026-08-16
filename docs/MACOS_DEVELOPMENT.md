# KuroganeOS 2.6.1 — rozwój na macOS

KuroganeOS ma natywny workflow macOS dla kernela, Ring-3 userspace, SDK,
Kurogane Flux Desktop oraz development IMG uruchamianego przez QEMU. Nie wymaga
WSL ani Windows PowerShell. Host może być Apple Silicon albo Intel; target
KuroganeOS pozostaje `x86_64`.

> **Stan 2.6.1:** development IMG + QEMU jest główną ścieżką testową na macOS.
> Builder ISO istnieje, ale instalowalne ISO nadal ma znany problem runtime i
> jest traktowane jako osobny tor naprawy. Nie blokuje to rozwoju desktopu.

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

## 2. Zwykły build development IMG

Pełny debug rebuild:

```bash
./scripts/build-macos.sh --configuration debug --rebuild
```

Release IMG:

```bash
./scripts/build-macos.sh --configuration release --rebuild
```

Wyniki 2.6.1:

```text
build/kernel.elf
build/BOOTX64.EFI
build/sdk/sysroot/
build/userspace/rootfs/
build/images/KuroganeOS-macos.img
dist/KuroganeOS-2.6.1-macos-qemu.img
```

## 3. QEMU — duży tryb desktopowy

Najwygodniejsza komenda do pracy nad GUI:

```bash
./scripts/run-qemu-macos.sh \
  --image ./dist/KuroganeOS-2.6.1-macos-qemu.img \
  --display
```

Od 2.6.1 `--display` oznacza:

- Cocoa frontend;
- `zoom-to-fit`;
- fullscreen jako domyślny wizualny development mode;
- 768 MiB RAM;
- jawne Standard VGA;
- pozostawienie QEMU uruchomionego po przejściu smoke testu.

Jeżeli wolisz normalne okno, ale nadal skalowane:

```bash
./scripts/run-qemu-macos.sh \
  --image ./dist/KuroganeOS-2.6.1-macos-qemu.img \
  --windowed
```

Jawny fullscreen:

```bash
./scripts/run-qemu-macos.sh \
  --image ./dist/KuroganeOS-2.6.1-macos-qemu.img \
  --fullscreen
```

Automatyczny headless smoke test:

```bash
./scripts/run-qemu-macos.sh
```

Apple Silicon uruchamia gościa x86-64 przez QEMU TCG.

## 4. Flux Terminal 2.6.1

GUI Terminal ma obecnie m.in.:

```text
help
version
uname
pid
pwd
cat /absolute/path
read /absolute/path
which <name>
apps
run <app>
gui <surface>
open <path|app>
jobs
wait <pid>
history
status
echo
about
clear
```

`cat/read/which/open` korzystają z publicznego Ring-3 ABI. Nie ma przejścia do
kernelowego shella.

Komendy wymagające przyszłego publicznego VFS capability ABI (`ls`, `stat`,
`cd`, `mkdir`, `touch`, `rm`, `cp`, `mv`) są rozpoznawane, ale nie udają
powodzenia.

## 5. Files 2.6.1

Files udostępnia VFS-backed Quick Access:

- `J/K` — wybór pozycji;
- `ENTER` — podgląd albo uruchomienie aplikacji;
- `R` — ponowny odczyt podglądu;
- `/etc/system.cfg` jest czytany przez `open/read/close`;
- obrazy ELF64 są rozpoznawane jako executable images.

Pełne `readdir/stat` i prawdziwa nawigacja katalogów wymagają następnego
rozszerzenia publicznego ABI.

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

Następnie w Flux Terminal:

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

## 8. ISO — aktualny stan

Natywny builder nadal istnieje:

```bash
./scripts/build-macos.sh --configuration release --rebuild --iso
```

oraz:

```bash
./scripts/build-installer-macos.sh --configuration release --rebuild
```

ale w 2.6.1 **nie traktujemy wygenerowanego ISO jako zweryfikowanego release
medium**, dopóki jego obecny problem runtime nie zostanie naprawiony i przejdzie
osobny installer smoke test.

Do bieżącego developmentu używaj:

```text
dist/KuroganeOS-2.6.1-macos-qemu.img
```

## 9. Zalecany cykl pracy

```bash
git pull origin main
./scripts/build-macos.sh --configuration debug --rebuild
./scripts/run-qemu-macos.sh \
  --image ./dist/KuroganeOS-2.6.1-macos-qemu.img \
  --display
```

Jeśli testujesz tylko zmiany userspace/SDK po istniejącym kernel buildzie, można
użyć `--stage-only`, ale po zmianie wersji lub kernela zawsze preferowany jest
pełny `--rebuild`.

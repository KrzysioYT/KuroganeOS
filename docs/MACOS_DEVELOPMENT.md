# KuroganeOS 3.3.0-dev — rozwój na macOS

KuroganeOS ma natywny workflow macOS dla x86-64 kernela, Ring-3 userspace,
SDK, Red Flux Desktop oraz media IMG/ISO. Apple Silicon i Intel są hostami;
target pozostaje `x86_64`.

## Przygotowanie

```bash
chmod +x scripts/*.sh
./scripts/setup-macos.sh --install
```

Środowisko obejmuje m.in. `x86_64-elf-gcc/binutils`, QEMU, mtools,
dosfstools, gptfdisk, xorriso i Python.

## Zalecany build 3.3 DEV BETA

Od 3.3 główna komenda buduje **oba nośniki** i dodaje `install.pkg` także do
QEMU IMG:

```bash
bash ./scripts/build-media-macos.sh --configuration release --rebuild
```

Do szybszych buildów debugowych:

```bash
bash ./scripts/build-media-macos.sh --configuration debug --rebuild
```

Wyniki:

```text
dist/KuroganeOS-3.3.0-dev-macos-qemu.img
dist/KuroganeOS-3.3.0-dev-x86_64.iso
dist/SHA256SUMS.txt
```

IMG i ISO wchodzą do tego samego Red Flux Setup:

```text
Try KuroganeOS
Install KuroganeOS
```

## Kernel/userspace-only development

Niższy poziom nadal można budować bez produkowania pełnego media-setu:

```bash
./scripts/build-macos.sh --configuration debug --rebuild
```

Po zmianie tylko userspace/SDK i istniejącym kernelu można użyć:

```bash
./scripts/build-macos.sh --configuration debug --stage-only
```

Do wydawania/testowania 3.3 preferuj jednak `build-media-macos.sh`, ponieważ
zwykły development IMG nie jest gwarantowanym Try/Install medium dopóki wrapper
nie wstrzyknie `install.pkg`.

## QEMU

Bezpośredni wariant, który jest najmniej zależny od wrappera QEMU:

```bash
cp "$(brew --prefix qemu)/share/qemu/edk2-i386-vars.fd" /tmp/kurogane-vars.fd

qemu-system-x86_64 \
  -machine q35,accel=tcg \
  -cpu max \
  -m 768 \
  -vga std \
  -drive if=pflash,format=raw,unit=0,readonly=on,file="$(brew --prefix qemu)/share/qemu/edk2-x86_64-code.fd" \
  -drive if=pflash,format=raw,unit=1,file=/tmp/kurogane-vars.fd \
  -drive if=none,id=kurogane_media,format=raw,file="./dist/KuroganeOS-3.3.0-dev-macos-qemu.img",cache=writeback \
  -device ide-hd,drive=kurogane_media,bus=ide.0,bootindex=1 \
  -display cocoa \
  -serial stdio \
  -net none \
  -no-reboot \
  -no-shutdown
```

Apple Silicon wykonuje x86-64 guest przez TCG.

## Test instalacji

Do testu `Install KuroganeOS` dodaj **drugi pusty dysk przez AHCI/SATA**. Nośnik
IMG/ISO jest medium startowym i nie powinien być jednocześnie targetem
instalacji.

## Własne aplikacje

```bash
./scripts/build-app-macos.sh moja-aplikacja.c -o moja-aplikacja --install
./scripts/build-macos.sh --configuration debug --stage-only
```

Po wejściu do desktopu aplikację można uruchomić z Flux Terminala.

## Makefile

```bash
make kernel CONFIG=debug
make verify CONFIG=debug
make clean
```

## Status

3.3.0-dev jest DEV BETA. Nowy wspólny Try/Install flow, live package VFS oraz
ISO wymagają świeżego runtime acceptance na macOS przed oznaczeniem ich jako
zweryfikowane release media.

# KuroganeOS 3.3.1-dev — rozwój na macOS

KuroganeOS ma natywny workflow macOS dla x86-64 kernela, Ring-3 userspace,
SDK, Red Flux Desktop oraz media IMG/ISO. Apple Silicon i Intel są hostami;
target KuroganeOS pozostaje `x86_64`.

> Na Apple Silicon KuroganeOS x86-64 uruchamiaj przez QEMU/TCG. VirtualBox jest
> referencyjnym targetem dla x86-64 hostów Intel/AMD, nie dla M-series jako
> x86-64 guest.

## Przygotowanie

```bash
chmod +x scripts/*.sh
bash ./scripts/setup-macos.sh --install
```

Środowisko obejmuje m.in. `x86_64-elf-gcc/binutils`, QEMU, mtools,
dosfstools, gptfdisk, xorriso i Python.

## Zalecany build 3.3.1 DEV BETA

Główna komenda buduje **oba nośniki** i dodaje `install.pkg` także do QEMU IMG:

```bash
bash ./scripts/build-media-macos.sh --configuration release --rebuild
```

Do buildów debugowych:

```bash
bash ./scripts/build-media-macos.sh --configuration debug --rebuild
```

Wyniki:

```text
dist/KuroganeOS-3.3.1-dev-macos-qemu.img
dist/KuroganeOS-3.3.1-dev-x86_64.iso
dist/SHA256SUMS.txt
```

IMG i ISO wchodzą do tego samego Red Flux Setup:

```text
Try KuroganeOS
Install KuroganeOS
```

Builder ISO 3.3.1 używa 30 MiB FAT16 El Torito EFI image, GPT ESP oraz
obowiązkowego 20-pass verifiera. Szczegóły: [`VIRTUALBOX.md`](VIRTUALBOX.md).

## Kernel/userspace-only development

Niższy poziom nadal można budować bez produkowania pełnego media-setu:

```bash
./scripts/build-macos.sh --configuration debug --rebuild
```

Po zmianie tylko userspace/SDK i istniejącym kernelu:

```bash
./scripts/build-macos.sh --configuration debug --stage-only
```

Do wydawania/testowania pełnego nośnika preferuj `build-media-macos.sh`.

## QEMU — IMG

```bash
cp "$(brew --prefix qemu)/share/qemu/edk2-i386-vars.fd" /tmp/kurogane-vars.fd

qemu-system-x86_64 \
  -machine q35,accel=tcg \
  -cpu max \
  -m 1024 \
  -vga std \
  -drive if=pflash,format=raw,unit=0,readonly=on,file="$(brew --prefix qemu)/share/qemu/edk2-x86_64-code.fd" \
  -drive if=pflash,format=raw,unit=1,file=/tmp/kurogane-vars.fd \
  -drive if=none,id=kurogane_media,format=raw,file="./dist/KuroganeOS-3.3.1-dev-macos-qemu.img",cache=writeback \
  -device ide-hd,drive=kurogane_media,bus=ide.0,bootindex=1 \
  -display cocoa \
  -serial stdio \
  -net none \
  -no-reboot \
  -no-shutdown
```

## QEMU — test sieci E1000/NAT

Do testowania kernelowego internetu zamiast `-net none` użyj:

```text
-netdev user,id=net0
-device e1000,netdev=net0
```

Czyli zastąp `-net none` powyższymi dwoma argumentami. KuroganeOS oczekuje
Intel E1000/82540EM i pobiera konfigurację przez DHCP. Jeżeli DHCP nie jest
dostępne, 3.3.1 przechodzi do loopback zamiast zatrzymywać desktop.

## QEMU — optical smoke ISO

Możesz sprawdzić, czy niezależny firmware UEFI rzeczywiście bootuje ISO:

```bash
bash ./scripts/smoke-uefi-iso-qemu.sh \
  ./dist/KuroganeOS-3.3.1-dev-x86_64.iso \
  --timeout 60
```

Helper używa split CODE/VARS pflash firmware z QEMU/Homebrew i czeka na marker
kernela przez port szeregowy.

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

Dokumentacja aplikacji: [`DEVELOPERS/README.md`](DEVELOPERS/README.md).

## Makefile

```bash
make kernel CONFIG=debug
make verify CONFIG=debug
make clean
```

## Status

3.3.1-dev jest DEV BETA. Struktura ISO ma automatyczny verifier i niezależny
OVMF/QEMU optical smoke. Realny VirtualBox smoke wykonuj na hoście x86-64.

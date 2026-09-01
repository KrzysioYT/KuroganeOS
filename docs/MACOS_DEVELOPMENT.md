# KuroganeOS 3.3.3-dev — development na macOS

Target KuroganeOS pozostaje `x86_64 + UEFI`. macOS może być hostem Intel albo
Apple Silicon. Na Apple Silicon x86-64 guest działa przez **QEMU TCG**, więc nie
używaj tego środowiska jako benchmarku FPS Forged Steel compositora.

## Setup

```bash
chmod +x scripts/*.sh
bash ./scripts/setup-macos.sh --install
```

Setup instaluje/wykrywa m.in. cross GCC/binutils, QEMU, mtools, dosfstools,
gptfdisk, xorriso i Python.

## Development build

```bash
bash ./scripts/build-macos.sh --configuration debug --rebuild
```

Foundation image:

```text
build/images/KuroganeOS-base.img
```

To jest właściwy obraz do `/system/init`, loginu i pełnego desktopu.

## Pełne media

```bash
bash ./scripts/build-media-macos.sh --configuration release --rebuild
```

Bieżący macOS media pipeline publikuje:

```text
dist/KuroganeOS-3.3.3-dev-macos-qemu.img
dist/KuroganeOS-3.3.3-dev-x86_64.iso
dist/SHA256SUMS.txt
```

## Interaktywny QEMU

Najprościej:

```bash
./scripts/run-qemu-macos.sh \
  --image ./build/images/KuroganeOS-base.img \
  --windowed \
  --memory 1024
```

Albo bez `--image`:

```bash
./scripts/run-qemu-macos.sh --windowed --memory 1024
```

Runner szuka kolejno:

1. najnowszego `dist/*-macos-qemu.img`;
2. `state/KuroganeOS.img`;
3. `build/images/KuroganeOS-base.img`.

Fullscreen:

```bash
./scripts/run-qemu-macos.sh --display --memory 1024
```

## Headless smoke

```bash
./scripts/run-qemu-macos.sh \
  --image ./build/images/KuroganeOS-base.img \
  --timeout 90
```

Headless runner czeka na markery kernela, PID1, desktop session i graficznego
loginu. Nie wymaga starego tekstowego promptu użytkownika.

## Konfiguracja QEMU używana przez wrapper

```text
machine:       q35
accelerator:   tcg
cpu:           max
RAM:           1024 MiB domyślnie
firmware:      EDK2 split CODE/VARS pflash
storage:       IDE attachment of raw GPT image
network:       E1000 + QEMU user IPv4 NAT
audio:         Intel AC97 -> CoreAudio
display:       Cocoa zoom-to-fit
```

Na macOS wrapper celowo używa TCG dla x86-64 guest. KVM/WHPX nie istnieją na tym
hoście.

## Kurogane Web

E1000/NAT jest domyślnie włączone w macOS runnerze. Bieżący Kurogane Web ma
HTTP oraz HTTPS/TLS i systemowy trust store eksportowany przez macOS build
pipeline.

## Optical smoke ISO

```bash
bash ./scripts/smoke-uefi-iso-qemu.sh \
  ./dist/KuroganeOS-3.3.3-dev-x86_64.iso \
  --timeout 90
```

## Aplikacje

```bash
./scripts/build-app-macos.sh moja-aplikacja.c -o moja-aplikacja --install
./scripts/build-macos.sh --configuration debug --stage-only
```

Aplikacje są statycznymi ELF64 Ring-3 dla KuroganeOS, nie binariami macOS ani
Linux.

## Diagnostyka

Logi macOS runnera:

```text
build/logs/qemu-macos-serial.log
build/logs/qemu-macos-stdout.log
build/logs/qemu-macos-stderr.log
```

Przy problemie najpierw sprawdź serial:

```bash
tail -n 160 build/logs/qemu-macos-serial.log
```

Dalsze informacje: [RUNNING.md](RUNNING.md), [QEMU_TESTING.md](QEMU_TESTING.md)
i [TESTING.md](TESTING.md).

# KuroganeOS 3.3.3-dev — Linux development

KuroganeOS ma natywny workflow dla hostów **Linux x86-64**. Linux jest wyłącznie
hostem build/test; target pozostaje własnym `x86_64 + UEFI` KuroganeOS.

## Setup

```bash
chmod +x scripts/*.sh
bash ./scripts/setup-linux.sh --install
```

Wymagane są m.in.:

```text
gcc/g++/binutils/make
python3
qemu-system-x86_64
mtools
dosfstools
sgdisk
xorriso
```

Jeżeli `x86_64-elf-gcc` jest dostępny, jest preferowany. Host GCC może być
fallbackiem tylko na odpowiednio wspieranym hoście x86-64.

## Development build

```bash
bash ./scripts/build-linux.sh --configuration debug --rebuild
```

Pełny userspace testuje Foundation image:

```text
build/images/KuroganeOS-base.img
```

Legacy `kurogane.img` jest FAT/EFI artifactem i nie zastępuje Foundation GPT z
`Kurogane Root`.

## Pełne media

```bash
bash ./scripts/build-media-linux.sh --configuration release --rebuild
```

Dokładne nazwy wyników sprawdź w outputcie bieżącego buildera i
`dist/SHA256SUMS.txt`; historyczne dokumenty release mogą zawierać starsze
schematy nazw.

## QEMU

Na natywnym Linux x86-64 do ręcznego GUI preferuj KVM, jeżeli host udostępnia
`/dev/kvm`. Deterministyczne CI może używać TCG.

Minimalny przykład dla Foundation:

```bash
qemu-system-x86_64 \
  -machine q35,accel=kvm:tcg \
  -m 1024 \
  -smp 1 \
  -drive if=none,id=kurogane,format=raw,file=./build/images/KuroganeOS-base.img,snapshot=on \
  -device ide-hd,drive=kurogane,bus=ide.0,bootindex=1 \
  -netdev user,id=net0 \
  -device e1000,netdev=net0
```

Do realnego bootu potrzebujesz również EDK2/OVMF pflash CODE/VARS zgodnie z
lokalizacją pakietu na swojej dystrybucji.

## UEFI optical smoke

```bash
bash ./scripts/smoke-uefi-iso-qemu.sh \
  ./dist/<aktualne-kurogane-iso> \
  --timeout 90
```

## VirtualBox x86-64

Jeżeli `VBoxManage` jest dostępny, użyj bieżącego VirtualBox ISO i helpera:

```bash
bash ./scripts/create-virtualbox-vm.sh --iso ./dist/<aktualne-virtualbox-iso>
```

Profil powinien mieć EFI, SATA/AHCI, NAT + E1000 82540EM oraz AC'97.

## Sieć / Web

Referencyjny NIC:

```text
Intel E1000 / 82540EM
```

KuroganeOS ma DHCP/DNS/ICMP/TCP oraz transport HTTP/HTTPS używany przez
Kurogane Web. Do testu online w QEMU używaj user-mode NAT i Foundation image.

## GUI performance

TCG nie jest miarodajnym benchmarkiem software compositora. Na Linux x86-64 z
KVM różnica może być bardzo duża. Bieżąca optymalizacja dotyczy input latency,
redukcji redraw oraz kosztu GOP scanout.

## Status

Publiczna wersja pozostaje `3.3.3-dev`. Gałąź Forged Steel nie jest jeszcze
release 5.0.0. Aktualny status kwalifikacji opisuje [BUILD_STATUS.md](BUILD_STATUS.md),
a procedurę testową [TESTING.md](TESTING.md).

# KuroganeOS 3.3.1-dev — Linux development

KuroganeOS ma natywny workflow dla hostów **Linux x86-64**. KuroganeOS nadal
jest osobnym systemem x86-64/UEFI; Linux jest wyłącznie hostem build/test.

## Zależności

Skrypt rozpoznaje apt, dnf lub pacman:

```bash
bash ./scripts/setup-linux.sh --install
```

Wymagane są m.in.:

```text
gcc / g++ / binutils / make
python3
qemu-system-x86_64
mtools
dosfstools
sgdisk
xorriso
```

Na x86-64 skrypty mogą użyć hostowego GNU toolchainu w trybie freestanding.
Jeśli w PATH istnieje `x86_64-elf-gcc`, jest preferowany.

Na hostach Linux innych niż x86-64 wymagany jest dedykowany x86_64-elf
cross-toolchain; automatyczny fallback host GCC jest tam wyłączony.

## Pełny media build

```bash
bash ./scripts/build-media-linux.sh --configuration release --rebuild
```

Wynik:

```text
dist/KuroganeOS-3.3.1-dev-linux-qemu.img
dist/KuroganeOS-3.3.1-dev-x86_64.iso
dist/SHA256SUMS.txt
```

Oba nośniki uruchamiają Red Flux Setup z `Try KuroganeOS` i
`Install KuroganeOS`.

ISO przechodzi obowiązkowo 20-pass El Torito/FAT/GPT/PE verifier przed
publikacją do `dist/`.

## Development-only build

Bez pełnego media setu:

```bash
bash ./scripts/build-linux.sh --configuration debug --rebuild
```

Można też użyć:

```bash
bash ./scripts/build.sh debug
bash ./scripts/build.sh rebuild
bash ./scripts/build.sh media
```

W prawdziwym WSL `build.sh` zachowuje Windows/PowerShell workflow. Na natywnym
Linuxie przechodzi do `build-linux.sh`.

## UEFI optical smoke ISO

Jeżeli masz OVMF:

```bash
bash ./scripts/smoke-uefi-iso-qemu.sh \
  ./dist/KuroganeOS-3.3.1-dev-x86_64.iso \
  --timeout 60
```

Helper używa oddzielnych pflash CODE + writable VARS i czeka na marker kernela
przez serial. To jest niezależny test od analizy struktury ISO.

## VirtualBox na Linux x86-64

Jeżeli masz Oracle VirtualBox z `VBoxManage`, możesz utworzyć referencyjną VM:

```bash
bash ./scripts/create-virtualbox-vm.sh \
  --iso ./dist/KuroganeOS-3.3.1-dev-x86_64.iso
```

Profil ustawia EFI64, SATA/AHCI, NAT + 82540EM oraz Intel AC'97.

## Internet

Referencyjna karta:

```text
Intel E1000 / 82540EM
```

Dla QEMU możesz użyć:

```text
-netdev user,id=net0
-device e1000,netdev=net0
```

Kernel pobiera DHCP i ma DNS/ICMP/basic TCP probe. Brak DHCP nie zatrzymuje już
całego desktopu — system przechodzi do loopback fallback.

## Status

Linux jest aktywnie kwalifikowany przez GitHub Actions. Workflow buduje pełny
IMG/ISO, wykonuje verifier oraz realny optical UEFI boot przez OVMF/QEMU.
Prawdziwy VirtualBox smoke nadal wymaga hosta x86-64 z zainstalowanym
VirtualBox.

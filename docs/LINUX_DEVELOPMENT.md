# KuroganeOS 3.3.0-dev — Linux development

3.3 wprowadza natywny workflow dla hostów **Linux x86-64**. KuroganeOS nadal
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
dist/KuroganeOS-3.3.0-dev-linux-qemu.img
dist/KuroganeOS-3.3.0-dev-x86_64.iso
dist/SHA256SUMS.txt
```

Oba nośniki uruchamiają Red Flux Setup z `Try KuroganeOS` i
`Install KuroganeOS`.

## Development-only build

Bez ISO/setup injection:

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

## QEMU

Przykładowo z dystrybucyjnym OVMF można uruchomić wersjonowany IMG jako boot
media. Dokładna ścieżka OVMF zależy od dystrybucji, dlatego repo nie koduje
jednej globalnej ścieżki Linux.

Do testu instalacji należy dodać osobny pusty dysk SATA/AHCI. Nie używaj media
IMG jako jednoczesnego targetu instalatora.

## Status

Linux support w 3.3 jest nową ścieżką DEV BETA i wymaga świeżego build/runtime
acceptance na realnym Linux x86-64 przed uznaniem za stabilny host release.

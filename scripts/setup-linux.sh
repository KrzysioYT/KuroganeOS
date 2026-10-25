#!/usr/bin/env bash
set -euo pipefail

install=false
if [[ "${1:-}" == "--install" ]]; then
    install=true
elif [[ $# -ne 0 ]]; then
    echo "usage: bash ./scripts/setup-linux.sh [--install]" >&2
    exit 2
fi

if [[ "$(uname -s)" != Linux ]]; then
    echo "setup-linux.sh requires Linux." >&2
    exit 1
fi

arch="$(uname -m)"
if [[ "$arch" != x86_64 ]]; then
    echo "KuroganeOS currently needs an x86_64-elf cross-toolchain on Linux/$arch." >&2
    echo "Automatic host-GNU fallback is available only on x86_64 Linux." >&2
    exit 1
fi

run_install() {
    if command -v apt-get >/dev/null 2>&1; then
        sudo apt-get update
        sudo apt-get install -y build-essential binutils python3 \
            qemu-system-x86 mtools dosfstools gdisk xorriso
    elif command -v dnf >/dev/null 2>&1; then
        sudo dnf install -y gcc gcc-c++ binutils make python3 \
            qemu-system-x86 mtools dosfstools gdisk xorriso
    elif command -v pacman >/dev/null 2>&1; then
        sudo pacman -S --needed base-devel binutils python qemu-system-x86 \
            mtools dosfstools gptfdisk libisoburn
    else
        echo "Unsupported package manager. Install GNU gcc/g++, binutils, make, python3," >&2
        echo "qemu-system-x86_64, mtools, dosfstools, sgdisk and xorriso manually." >&2
        exit 1
    fi
}

if $install; then run_install; fi

missing=0
for tool in gcc g++ ld objcopy readelf ar make python3 qemu-system-x86_64 \
            mcopy mmd mdir mkfs.fat sgdisk xorriso dd truncate sha256sum; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "missing: $tool" >&2
        missing=1
    fi
done
if [[ $missing -ne 0 ]]; then
    echo "Linux KuroganeOS environment is incomplete." >&2
    echo "Run: bash ./scripts/setup-linux.sh --install" >&2
    exit 1
fi

echo "[linux] KuroganeOS build environment ready on $(uname -srmo)"
echo "[linux] native x86-64 GNU freestanding toolchain will be used unless CC/CXX/LD override it"

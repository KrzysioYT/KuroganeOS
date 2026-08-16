#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "setup-macos.sh is intended for macOS." >&2
    exit 1
fi

install=false
if [[ "${1:-}" == "--install" ]]; then
    install=true
elif [[ $# -ne 0 ]]; then
    echo "usage: ./scripts/setup-macos.sh [--install]" >&2
    exit 2
fi

packages=(
    x86_64-elf-binutils
    x86_64-elf-gcc
    qemu
    mtools
    dosfstools
    gptfdisk
    xorriso
    python
)

if ! command -v brew >/dev/null 2>&1; then
    cat >&2 <<'EOF'
Homebrew was not found.
Install Homebrew first, then run:
  ./scripts/setup-macos.sh --install
EOF
    exit 1
fi

if $install; then
    echo "[macos] installing KuroganeOS development dependencies..."
    brew install "${packages[@]}"
fi

required=(
    x86_64-elf-gcc x86_64-elf-g++ x86_64-elf-ld
    x86_64-elf-objcopy x86_64-elf-readelf x86_64-elf-ar
    qemu-system-x86_64 mcopy mmd mdir mkfs.fat fsck.fat sgdisk python3
)
missing=()
for tool in "${required[@]}"; do
    command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
done

if ((${#missing[@]})); then
    echo "[macos] missing tools: ${missing[*]}" >&2
    echo "[macos] install with: brew install ${packages[*]}" >&2
    exit 1
fi

arch="$(uname -m)"
echo "[macos] host architecture: $arch"
echo "[macos] cross compiler: $(x86_64-elf-gcc --version | head -n 1)"
echo "[macos] QEMU: $(qemu-system-x86_64 --version | head -n 1)"
echo "[macos] toolchain ready"
echo
echo "Next:"
echo "  ./scripts/build-macos.sh --configuration debug"
echo "  ./scripts/run-qemu-macos.sh"

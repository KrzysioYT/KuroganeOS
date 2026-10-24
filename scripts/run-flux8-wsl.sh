#!/usr/bin/env bash
# Flux 8 manual desktop harness for Ubuntu WSLg, with real USB Tablet input.
# Safe default: QEMU snapshot=on, no physical disk modifications.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
build=false
persistent=false
memory=1024
while (($#)); do
    case "$1" in
        --build) build=true; shift ;;
        --persistent) persistent=true; shift ;;
        --memory) [[ $# -ge 2 ]] || exit 2; memory="$2"; shift 2 ;;
        -h|--help)
            echo "usage: bash scripts/run-flux8-wsl.sh [--build] [--persistent] [--memory 512..8192]"
            echo "  --build       rebuild release IMG/ISO first"
            echo "  --persistent  use a private disposable IMG copy across boots (for settings restore)"
            echo "  default       QEMU snapshot (no writes retained to the source IMG)"
            exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
[[ "$memory" =~ ^[0-9]+$ ]] && ((memory >= 512 && memory <= 8192)) || {
    echo "RAM must be between 512 and 8192 MiB" >&2; exit 2;
}
[[ "$(uname -s)" == Linux ]] || { echo "Requires Linux/WSL2" >&2; exit 1; }
if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
    echo "No WSLg GUI detected; check DISPLAY/WAYLAND_DISPLAY before opening QEMU" >&2
    exit 1
fi
command -v qemu-system-x86_64 >/dev/null || {
    echo "Missing qemu-system-x86_64 (sudo apt install qemu-system-x86 qemu-system-gui)" >&2
    exit 1
}
if $build; then
    bash scripts/build-media-linux.sh --configuration release --rebuild
fi
source_image="$root/build/images/KuroganeOS-linux.img"
[[ -s "$source_image" ]] || {
    echo "No built live image at $source_image" >&2
    echo "Run with --build or build with scripts/build-media-linux.sh" >&2
    exit 1
}
if [[ -f /usr/share/OVMF/OVMF_CODE_4M.fd &&
      -f /usr/share/OVMF/OVMF_VARS_4M.fd ]]; then
    code=/usr/share/OVMF/OVMF_CODE_4M.fd
    vars=/usr/share/OVMF/OVMF_VARS_4M.fd
elif [[ -f /usr/share/OVMF/OVMF_CODE.fd &&
        -f /usr/share/OVMF/OVMF_VARS.fd ]]; then
    code=/usr/share/OVMF/OVMF_CODE.fd
    vars=/usr/share/OVMF/OVMF_VARS.fd
else
    echo "OVMF firmware unavailable (sudo apt install ovmf)" >&2
    exit 1
fi
test_dir="$root/build/tests/flux8-manual"
mkdir -p "$test_dir"
if [[ ! -s "$test_dir/OVMF_VARS.fd" ]] ||
   [[ "$(stat -c %s "$test_dir/OVMF_VARS.fd")" != "$(stat -c %s "$vars")" ]]; then
    cp "$vars" "$test_dir/OVMF_VARS.fd"
fi

image="$source_image"
snapshot=on
if $persistent; then
    # Never write to build/images or the physical Windows disk. The copy is
    # deliberately retained so selected themes survive a VM reboot.
    image="$test_dir/flux8-persistent.img"
    if [[ ! -e "$image" ]]; then
        cp --reflink=auto "$source_image" "$image"
    fi
    snapshot=off
fi
echo "[flux8] source : $source_image"
echo "[flux8] runtime: $image (snapshot=$snapshot)"
echo "[flux8] serial : $test_dir/serial.log"
echo "[flux8] pointer: xHCI USB Tablet (no PS/2)"
echo "[flux8] desktop: GTK via WSLg"
qemu-system-x86_64 \
    -machine q35,accel=tcg,i8042=off \
    -cpu max -smp 2 -m "$memory" \
    -vga std \
    -drive "if=pflash,format=raw,unit=0,readonly=on,file=$code" \
    -drive "if=pflash,format=raw,unit=1,file=$test_dir/OVMF_VARS.fd" \
    -drive "if=none,id=flux8disk,format=raw,file=$image,snapshot=$snapshot" \
    -device ide-hd,drive=flux8disk,bus=ide.0,bootindex=1 \
    -device qemu-xhci,id=kurogane_xhci \
    -device usb-kbd,bus=kurogane_xhci.0 \
    -device usb-tablet,bus=kurogane_xhci.0 \
    -net none \
    -serial "file:$test_dir/serial.log" \
    -display gtk \
    -no-reboot

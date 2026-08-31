#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image=""
scratch=""
image_explicit=0
writable=0
headless=0
gdb_port=1234

usage() {
    cat <<'EOF'
usage: run-qemu-debug.sh [options] [IMAGE]
  --writable          attach the explicitly named IMAGE without a snapshot
  --scratch PATH      attach an existing separate writable SATA data disk
  --headless          disable the QEMU display
  --gdb-port PORT     GDB TCP port (default: 1234)
EOF
}

while (($#)); do
    case "$1" in
        --writable) writable=1 ;;
        --scratch)
            (($# >= 2)) || { echo "--scratch requires a path" >&2; exit 2; }
            scratch="$2"
            shift
            ;;
        --headless) headless=1 ;;
        --gdb-port)
            (($# >= 2)) || { echo "--gdb-port requires a value" >&2; exit 2; }
            gdb_port="$2"
            shift
            ;;
        -h|--help) usage; exit 0 ;;
        --) shift; break ;;
        -*) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
        *)
            ((image_explicit == 0)) || {
                echo "only one system IMAGE may be selected" >&2
                exit 2
            }
            image="$1"
            image_explicit=1
            ;;
    esac
    shift
done
if (($#)); then
    ((image_explicit == 0 && $# == 1)) || {
        echo "only one system IMAGE may be selected" >&2
        exit 2
    }
    image="$1"
    image_explicit=1
fi

if [[ -z "$image" ]]; then
    if [[ -f "$root/state/KuroganeOS.img" ]]; then
        image="$root/state/KuroganeOS.img"
    else
        image="$root/build/images/KuroganeOS-base.img"
    fi
fi
if ((writable && !image_explicit)); then
    echo "--writable requires an explicit IMAGE path" >&2
    exit 2
fi
[[ -f "$image" ]] || { echo "system image not found: $image" >&2; exit 3; }
image="$(realpath -e -- "$image")"

args=(-ImagePath "$(wslpath -w "$image")" -GdbPort "$gdb_port")
((writable)) && args+=(-Writable)
((headless)) && args+=(-Headless)
if [[ -n "$scratch" ]]; then
    [[ -f "$scratch" ]] || {
        echo "scratch image not found: $scratch" >&2
        exit 3
    }
    scratch="$(realpath -e -- "$scratch")"
    args+=(-ScratchDiskPath "$(wslpath -w "$scratch")")
fi

powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -File "$(wslpath -w "$root/scripts/run-qemu-debug.ps1")" "${args[@]}"

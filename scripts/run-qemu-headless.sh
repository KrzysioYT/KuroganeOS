#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image=""
scratch=""
image_explicit=0
writable=0
shell_test=0
socket_test=0
safe_mode=0
desktop_mode=0
timeout=30

usage() {
    cat <<'EOF'
usage: run-qemu-headless.sh [options] [IMAGE]
  --writable          attach the explicitly named IMAGE without a snapshot
  --scratch PATH      attach an existing separate writable SATA data disk
  --shell-test        run the keyboard/shell integration scenario
    --socket-test       run the Ring-3 socket readiness/progression probe
  --safe              request safe mode
  --desktop           request the experimental desktop mode
  --timeout SECONDS   prompt/test timeout (default: 30)
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
        --shell-test) shell_test=1 ;;
        --socket-test) socket_test=1 ;;
        --safe) safe_mode=1 ;;
        --desktop) desktop_mode=1 ;;
        --timeout)
            (($# >= 2)) || { echo "--timeout requires a value" >&2; exit 2; }
            timeout="$2"
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
if ((safe_mode && desktop_mode)); then
    echo "--safe and --desktop are mutually exclusive" >&2
    exit 2
fi
[[ -f "$image" ]] || { echo "system image not found: $image" >&2; exit 3; }
image="$(realpath -e -- "$image")"

args=(-ImagePath "$(wslpath -w "$image")" -TimeoutSeconds "$timeout")
((writable)) && args+=(-Writable)
((shell_test)) && args+=(-ShellTest)
((socket_test)) && args+=(-SocketTest)
((safe_mode)) && args+=(-SafeMode)
((desktop_mode)) && args+=(-DesktopMode)
if [[ -n "$scratch" ]]; then
    [[ -f "$scratch" ]] || {
        echo "scratch image not found: $scratch" >&2
        exit 3
    }
    scratch="$(realpath -e -- "$scratch")"
    args+=(-ScratchDiskPath "$(wslpath -w "$scratch")")
fi

powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -File "$(wslpath -w "$root/scripts/run-qemu-headless.ps1")" "${args[@]}"

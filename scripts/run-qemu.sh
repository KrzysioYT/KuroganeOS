#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-interactive}"
foundation="$root/build/images/KuroganeOS-base.img"
working="$root/state/KuroganeOS.img"

case "$mode" in
    img)
        shift
        exec bash "$root/scripts/run-qemu-img.sh" "$@"
        ;;
    headless)
        shift
        exec bash "$root/scripts/run-qemu-headless.sh" "$@"
        ;;
    debug)
        shift
        exec bash "$root/scripts/run-qemu-debug.sh" "$@"
        ;;
    interactive|desktop)
        [[ -f "$working" || -f "$foundation" ]] || {
            echo "Foundation image not found; run a build first." >&2
            exit 3
        }
        image="$foundation"
        [[ -f "$working" ]] && image="$working"
        extra=(
            -UseDiskImage
            -DiskImagePath "$(wslpath -w "$image")"
            -Display
            -KeepRunning
            -MemoryMiB 1024
            -LogName desktop
        )
        ;;
    system)
        [[ -f "$foundation" ]] || {
            echo "Foundation image not found: $foundation" >&2
            exit 3
        }
        extra=(
            -UseDiskImage
            -DiskImagePath "$(wslpath -w "$foundation")"
            -ShellTest
            -TimeoutSeconds 90
            -MemoryMiB 1024
            -LogName system
        )
        ;;
    safe)
        [[ -f "$foundation" ]] || {
            echo "Foundation image not found: $foundation" >&2
            exit 3
        }
        extra=(
            -UseDiskImage
            -DiskImagePath "$(wslpath -w "$foundation")"
            -SafeMode
            -ShellTest
            -TimeoutSeconds 60
            -MemoryMiB 512
            -LogName safe
        )
        ;;
    iso)
        extra=(-UseIso -Display -KeepRunning -MemoryMiB 1024 -LogName iso)
        ;;
    smoke)
        extra=(-Display -TimeoutSeconds 30 -LogName smoke)
        ;;
    fast)
        shift
        exec powershell.exe -NoProfile -ExecutionPolicy Bypass \
            -File "$(wslpath -w "$root/scripts/run-qemu-fast.ps1")" "$@"
        ;;
    *)
        cat >&2 <<EOF
usage: $0 {interactive|desktop|system|safe|iso|smoke|fast|img|headless|debug} [mode options]

  interactive/desktop  open current working/base Foundation image and keep QEMU running
  system               deterministic Foundation graphical integration test
  safe                 Foundation safe-mode console integration test
  iso                  open the current ISO interactively
  smoke                staged EFI/FAT smoke boot
  fast                 Windows WHPX/TCG interactive runner
  img/headless/debug    dedicated image wrappers
EOF
        exit 2
        ;;
esac

powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -File "$(wslpath -w "$root/scripts/run-qemu.ps1")" "${extra[@]}"

#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-smoke}"
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
    smoke) extra=(-Display -LogName smoke) ;;
    system)
        extra=(-Display -ShellTest -UseDiskImage -TimeoutSeconds 30 -LogName system)
        ;;
    iso)
        extra=(-Display -UseIso -TimeoutSeconds 30 -LogName iso)
        ;;
    safe)
        extra=(-Display -ShellTest -SafeMode -UseDiskImage -TimeoutSeconds 30 -LogName safe)
        ;;
    desktop)
        extra=(-Display -ShellTest -DesktopMode -UseDiskImage -TimeoutSeconds 30 -LogName desktop)
        ;;
    *)
        echo "usage: $0 {smoke|system|iso|safe|desktop|img|headless|debug} [mode options]" >&2
        exit 2
        ;;
esac
powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -File "$(wslpath -w "$root/scripts/run-qemu.ps1")" "${extra[@]}"

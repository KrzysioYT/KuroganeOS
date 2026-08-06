#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-smoke}"
case "$mode" in
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
    *)
        echo "usage: $0 {smoke|system|iso|safe}" >&2
        exit 2
        ;;
esac
powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -File "$(wslpath -w "$root/scripts/run-qemu.ps1")" "${extra[@]}"

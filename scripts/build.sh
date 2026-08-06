#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-debug}"
case "$mode" in
    debug|release)
        args=(-Configuration "$mode")
        ;;
    rebuild)
        args=(-Configuration debug -Rebuild)
        ;;
    *)
        echo "usage: $0 {debug|release|rebuild}" >&2
        exit 2
        ;;
esac
powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -File "$(wslpath -w "$root/scripts/build.ps1")" "${args[@]}"

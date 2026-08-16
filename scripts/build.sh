#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-debug}"

# WSL remains the Windows frontend because the Windows repository-local
# x86_64-elf bundle and PowerShell image workflow are authoritative there.
if command -v powershell.exe >/dev/null 2>&1 && command -v wslpath >/dev/null 2>&1 && \
   grep -qi microsoft /proc/version 2>/dev/null; then
    case "$mode" in
        debug|release|test) args=(-Configuration "$mode") ;;
        rebuild) args=(-Configuration debug -Rebuild) ;;
        media)
            exec powershell.exe -NoProfile -ExecutionPolicy Bypass \
                -File "$(wslpath -w "$root/scripts/build-media.ps1")" \
                -Configuration release -Rebuild
            ;;
        *) echo "usage: $0 {debug|release|test|rebuild|media}" >&2; exit 2 ;;
    esac
    exec powershell.exe -NoProfile -ExecutionPolicy Bypass \
        -File "$(wslpath -w "$root/scripts/build.ps1")" "${args[@]}"
fi

if [[ "$(uname -s)" == Linux ]]; then
    case "$mode" in
        debug|release|test)
            exec bash "$root/scripts/build-linux.sh" --configuration "$mode"
            ;;
        rebuild)
            exec bash "$root/scripts/build-linux.sh" --configuration debug --rebuild
            ;;
        media)
            exec bash "$root/scripts/build-media-linux.sh" --configuration release --rebuild
            ;;
        *) echo "usage: $0 {debug|release|test|rebuild|media}" >&2; exit 2 ;;
    esac
fi

echo "scripts/build.sh is for Linux/WSL. On macOS use build-media-macos.sh." >&2
exit 1

#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if ! command -v powershell.exe >/dev/null 2>&1; then
    echo "powershell.exe is required for the repository-local cross toolchain" >&2
    exit 1
fi
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass \
    -File "$(wslpath -w "$root/scripts/build-sdk.ps1")"

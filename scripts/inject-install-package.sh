#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 GPT_IMAGE INSTALL_PACKAGE" >&2
    exit 2
fi

image="$1"
package="$2"
[[ -f "$image" ]] || { echo "missing GPT image: $image" >&2; exit 1; }
[[ -f "$package" ]] || { echo "missing installer package: $package" >&2; exit 1; }

for tool in dd mcopy mdir; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "required media tool is unavailable: $tool" >&2
        exit 1
    }
done

# Kurogane Foundation images use a fixed 64 MiB ESP beginning at LBA 2048.
# Extracting the partition before mtools access is portable across GNU/Linux,
# macOS/BSD dd and WSL and avoids relying on mtools' @@offset extension.
readonly esp_first=2048
readonly esp_sectors=131072
readonly sector_size=512

tmp="$(mktemp -d "${TMPDIR:-/tmp}/kurogane-media.XXXXXX")"
esp="$tmp/esp.img"
cleanup() { rm -rf -- "$tmp"; }
trap cleanup EXIT

dd if="$image" of="$esp" bs="$sector_size" skip="$esp_first" count="$esp_sectors" 2>/dev/null
mcopy -o -i "$esp" "$package" ::/install.pkg
mdir -i "$esp" ::/install.pkg >/dev/null
dd if="$esp" of="$image" bs="$sector_size" seek="$esp_first" conv=notrunc 2>/dev/null

echo "[media] install.pkg injected into ESP: $image"

#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
configuration=release
rebuild=false

usage() {
    echo "usage: bash ./scripts/build-media-macos.sh [--configuration debug|release|test] [--rebuild]" >&2
    exit 2
}
while (($#)); do
    case "$1" in
        --configuration) configuration="${2:-}"; shift 2 ;;
        --rebuild) rebuild=true; shift ;;
        *) usage ;;
    esac
done
case "$configuration" in debug|release|test) ;; *) usage ;; esac
[[ "$(uname -s)" == Darwin ]] || { echo "requires macOS" >&2; exit 1; }

args=(--configuration "$configuration")
if $rebuild; then args+=(--rebuild); fi
bash "$root/scripts/build-macos.sh" "${args[@]}"
bash "$root/scripts/build-installer-macos.sh" \
    --configuration "$configuration" --no-build

version="$(sed -n 's/^#define KUROGANE_VERSION_STRING "\([^"]*\)"/\1/p' common/version.h)"
image="$root/dist/KuroganeOS-$version-macos-qemu.img"
iso="$root/dist/KuroganeOS-$version-x86_64.iso"
package="$root/build/install.pkg"
[[ -f "$image" && -f "$iso" && -f "$package" ]] || {
    echo "media build did not produce IMG/ISO/install.pkg" >&2; exit 1; }

bash "$root/scripts/inject-install-package.sh" "$image" "$package"
image_hash="$(shasum -a 256 "$image" | awk '{print $1}')"
iso_hash="$(shasum -a 256 "$iso" | awk '{print $1}')"
printf '%s  %s\n%s  %s\n' \
    "$image_hash" "$(basename "$image")" \
    "$iso_hash" "$(basename "$iso")" > "$root/dist/SHA256SUMS.txt"
printf '%s  %s\n' "$image_hash" "$(basename "$image")" > "$image.sha256"

echo "[media-macos] KuroganeOS $version"
echo "[media-macos] live/setup IMG: $image"
echo "[media-macos] live/setup ISO: $iso"
echo "[media-macos] both media enter Try / Install setup"
echo "[media-macos] checksums: $root/dist/SHA256SUMS.txt"

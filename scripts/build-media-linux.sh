#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
configuration=release
rebuild=false

usage() {
    echo "usage: bash ./scripts/build-media-linux.sh [--configuration debug|release|test] [--rebuild]" >&2
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
[[ "$(uname -s)" == Linux ]] || { echo "requires Linux" >&2; exit 1; }

args=(--configuration "$configuration")
if $rebuild; then args+=(--rebuild); fi
bash "$root/scripts/build-linux.sh" "${args[@]}"
bash "$root/scripts/build-installer-linux.sh" \
    --configuration "$configuration" --no-build

version="$(bash "$root/scripts/read-version.sh")"
image="$root/dist/KuroganeOS-$version-linux-qemu.img"
iso="$root/dist/KuroganeOS-$version-x86_64.iso"
package="$root/build/install.pkg"
[[ -f "$image" && -f "$iso" && -f "$package" ]] || {
    echo "media build did not produce IMG/ISO/install.pkg" >&2; exit 1; }

bash "$root/scripts/inject-install-package.sh" "$image" "$package"
image_hash="$(sha256sum "$image" | awk '{print $1}')"
iso_hash="$(sha256sum "$iso" | awk '{print $1}')"
printf '%s  %s\n%s  %s\n' \
    "$image_hash" "$(basename "$image")" \
    "$iso_hash" "$(basename "$iso")" > "$root/dist/SHA256SUMS.txt"
printf '%s  %s\n' "$image_hash" "$(basename "$image")" > "$image.sha256"

echo "[media-linux] KuroganeOS $version"
echo "[media-linux] live/setup IMG: $image"
echo "[media-linux] live/setup ISO: $iso"
echo "[media-linux] both media enter Try / Install setup"
echo "[media-linux] checksums: $root/dist/SHA256SUMS.txt"

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

# build-macos.sh --iso is the canonical macOS media pipeline. It now builds the
# ISO and injects the same install.pkg into the live QEMU IMG, so both outputs
# enter Red Flux Setup with Try / Install and cannot silently diverge.
args=(--configuration "$configuration" --iso)
if $rebuild; then args+=(--rebuild); fi
bash "$root/scripts/build-macos.sh" "${args[@]}"

version="$(sed -n 's/^#define KUROGANE_VERSION_STRING "\([^"]*\)"/\1/p' common/version.h)"
image="$root/dist/KuroganeOS-$version-macos-qemu.img"
iso="$root/dist/KuroganeOS-$version-x86_64.iso"
package="$root/build/install.pkg"
[[ -f "$image" && -f "$iso" && -f "$package" ]] || {
    echo "media build did not produce unified IMG/ISO/install.pkg" >&2; exit 1; }

printf '[media-macos] KuroganeOS %s\n' "$version"
printf '[media-macos] live/setup IMG: %s\n' "$image"
printf '[media-macos] live/setup ISO: %s\n' "$iso"
printf '[media-macos] both media enter Try / Install setup\n'
printf '[media-macos] checksums: %s\n' "$root/dist/SHA256SUMS.txt"

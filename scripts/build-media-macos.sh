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

# build-macos.sh --iso is the canonical macOS media pipeline. It builds the
# Foundation/userspace image, installer package and ISO from the same source
# revision so Try/Install and the Forged Steel secure-session runtime do not
# silently diverge between media.
args=(--configuration "$configuration" --iso)
if $rebuild; then args+=(--rebuild); fi
bash "$root/scripts/build-macos.sh" "${args[@]}"

version="$(sed -n 's/^#define KUROGANE_VERSION_STRING "\([^"]*\)"/\1/p' common/version.h)"
image="$root/dist/KuroganeOS-$version-macos-qemu.img"
iso="$root/dist/KuroganeOS-$version-x86_64.iso"
package="$root/build/install.pkg"
[[ -f "$image" && -f "$iso" && -f "$package" ]] || {
    echo "media build did not produce IMG/ISO/install.pkg" >&2
    exit 1
}

printf '[media-macos] KuroganeOS %s\n' "$version"
printf '[media-macos] QEMU setup/install IMG: %s\n' "$image"
printf '[media-macos] install ISO: %s\n' "$iso"
printf '[media-macos] runtime: PID1 -> Secure Access -> Forged Steel desktop\n'
printf '[media-macos] checksums: %s\n' "$root/dist/SHA256SUMS.txt"

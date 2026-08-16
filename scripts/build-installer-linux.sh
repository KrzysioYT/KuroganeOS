#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
configuration=release
no_build=false
rebuild=false

usage() {
    cat >&2 <<'USAGE'
usage: bash ./scripts/build-installer-linux.sh [options]
  --configuration debug|release|test
  --rebuild
  --no-build
USAGE
    exit 2
}
while (($#)); do
    case "$1" in
        --configuration) configuration="${2:-}"; shift 2 ;;
        --rebuild) rebuild=true; shift ;;
        --no-build) no_build=true; shift ;;
        *) usage ;;
    esac
done
case "$configuration" in debug|release|test) ;; *) usage ;; esac
[[ "$(uname -s)" == Linux ]] || { echo "requires Linux" >&2; exit 1; }
if $rebuild && $no_build; then echo "--rebuild cannot be combined with --no-build" >&2; exit 2; fi

for tool in python3 mkfs.fat fsck.fat mcopy mmd mdir xorriso sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "missing installer tool: $tool" >&2
        echo "run: bash ./scripts/setup-linux.sh --install" >&2
        exit 1
    }
done

if ! $no_build; then
    args=(--configuration "$configuration" --no-image)
    if $rebuild; then args+=(--rebuild); fi
    bash "$root/scripts/build-linux.sh" "${args[@]}"
fi

kernel="$root/build/kernel.elf"
efi="$root/build/BOOTX64.EFI"
overlay="$root/build/userspace/rootfs"
package="$root/build/install.pkg"
stage="$root/build/installer-staging"
images="$root/build/images"
esp="$images/KuroganeOS-installer-esp.img"
internal_iso="$images/KuroganeOS-installer.iso"
[[ -f "$kernel" ]] || { echo "missing installer kernel: $kernel" >&2; exit 1; }
[[ -f "$efi" ]] || { echo "missing installer EFI: $efi" >&2; exit 1; }
[[ -d "$overlay" ]] || { echo "missing userspace overlay: $overlay" >&2; exit 1; }

version="$(sed -n 's/^#define KUROGANE_VERSION_STRING "\([^"]*\)"/\1/p' common/version.h)"
[[ -n "$version" ]] || { echo "cannot read version" >&2; exit 1; }
release_iso="$root/dist/KuroganeOS-$version-x86_64.iso"
compatibility_iso="$root/kurogane.iso"

python3 "$root/scripts/build-install-package.py" \
    --output "$package" --efi "$efi" --kernel "$kernel" \
    --rootfs "$root/rootfs" --overlay "$overlay"

rm -rf -- "$stage"
mkdir -p "$stage/EFI/BOOT" "$images" "$root/dist"
cp "$efi" "$stage/EFI/BOOT/BOOTX64.EFI"
cp "$kernel" "$stage/kernel.elf"
cp "$kernel" "$stage/EFI/BOOT/kernel.elf"
cp "$package" "$stage/install.pkg"

bash "$root/scripts/build-installer-esp.sh" "$stage" "$esp"
bash "$root/scripts/build-installer-iso.sh" "$stage" "$esp" "$internal_iso"
cp "$internal_iso" "$release_iso"
cp "$internal_iso" "$compatibility_iso"

echo "[installer-linux] ISO: $release_iso"
echo "[sha256] $(sha256sum "$release_iso" | awk '{print $1}')"

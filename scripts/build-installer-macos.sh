#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

configuration=release
no_build=false
rebuild=false

usage() {
    cat >&2 <<'USAGE'
usage: ./scripts/build-installer-macos.sh [options]
  --configuration debug|release|test
  --rebuild        clean macOS build outputs before compiling
  --no-build       reuse build/kernel.elf, build/BOOTX64.EFI and userspace overlay
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

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "build-installer-macos.sh requires macOS." >&2
    exit 1
fi
if $rebuild && $no_build; then
    echo "--rebuild cannot be combined with --no-build" >&2
    exit 2
fi

for tool in python3 mkfs.fat fsck.fat mcopy mmd mdir xorriso shasum; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "missing installer tool: $tool" >&2
        echo "run ./scripts/setup-macos.sh --install" >&2
        exit 1
    }
done

if ! $no_build; then
    build_args=(--configuration "$configuration" --no-image)
    if $rebuild; then build_args+=(--rebuild); fi
    bash "$root/scripts/build-macos.sh" "${build_args[@]}"
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
[[ -f "$efi" ]] || { echo "missing installer EFI loader: $efi" >&2; exit 1; }
[[ -d "$overlay" ]] || { echo "missing userspace overlay: $overlay" >&2; exit 1; }

version="$(sed -n 's/^#define KUROGANE_VERSION_STRING "\([^"]*\)"/\1/p' common/version.h)"
[[ -n "$version" ]] || { echo "cannot read KuroganeOS version" >&2; exit 1; }
release_name="KuroganeOS-$version-x86_64.iso"
release_iso="$root/dist/$release_name"
release_image_name="KuroganeOS-$version-macos-qemu.img"
release_image="$root/dist/$release_image_name"
checksum_file="$root/dist/SHA256SUMS.txt"
compatibility_iso="$root/kurogane.iso"

python3 "$root/scripts/build-install-package.py" \
    --output "$package" \
    --efi "$efi" \
    --kernel "$kernel" \
    --rootfs "$root/rootfs" \
    --overlay "$overlay"

case "$stage" in "$root/build/installer-staging") ;; *) echo "unsafe installer staging path" >&2; exit 1 ;; esac
case "$images" in "$root/build/images") ;; *) echo "unsafe installer image path" >&2; exit 1 ;; esac
rm -rf -- "$stage"
mkdir -p "$stage/EFI/BOOT" "$images" "$root/dist"
cp "$efi" "$stage/EFI/BOOT/BOOTX64.EFI"
cp "$kernel" "$stage/kernel.elf"
cp "$kernel" "$stage/EFI/BOOT/kernel.elf"
cp "$package" "$stage/install.pkg"

# El Torito EFI boot images have a 16-bit 512-byte sector count. Use the shared
# 30 MiB FAT16 builder instead of the historical 64 MiB FAT32 image.
bash "$root/scripts/build-installer-esp.sh" "$stage" "$esp"

bash "$root/scripts/build-installer-iso.sh" "$stage" "$esp" "$internal_iso"

cp "$internal_iso" "$release_iso"
cp "$internal_iso" "$compatibility_iso"

# Restore the pre-split media contract: when a live macOS QEMU IMG exists,
# inject the exact same install.pkg into its ESP. The single IMG can then boot
# Red Flux Setup and offer both Try KuroganeOS and Install KuroganeOS, while the
# ISO remains an equivalent optical/VirtualBox form of the same media.
image_hash=""
if [[ -f "$release_image" ]]; then
    bash "$root/scripts/inject-install-package.sh" "$release_image" "$package"
    image_hash="$(shasum -a 256 "$release_image" | awk '{print $1}')"
    printf '%s  %s\n' "$image_hash" "$release_image_name" > "$release_image.sha256"
    printf '[live-setup] %s\n' "$release_image"
else
    printf '[installer-macos] live IMG not present; ISO-only installer output\n' >&2
fi

iso_hash="$(shasum -a 256 "$release_iso" | awk '{print $1}')"
if [[ -n "$image_hash" ]]; then
    printf '%s  %s\n%s  %s\n' \
        "$image_hash" "$release_image_name" \
        "$iso_hash" "$release_name" > "$checksum_file"
else
    printf '%s  %s\n' "$iso_hash" "$release_name" > "$checksum_file"
fi

printf '[installer-macos] ESP: %s\n' "$esp"
printf '[installer-macos] internal ISO: %s\n' "$internal_iso"
printf '[release] %s\n' "$release_iso"
printf '[compatibility] %s\n' "$compatibility_iso"
printf '[sha256] %s\n' "$iso_hash"
printf '[checksums] %s\n' "$checksum_file"
if [[ -n "$image_hash" ]]; then
    printf '[media-macos] IMG and ISO both enter Red Flux Setup: Try / Install\n'
fi

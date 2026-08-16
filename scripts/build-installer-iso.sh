#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
stage="${1:?installer staging directory is required}"
esp_image="${2:?installer ESP image is required}"
iso_image="${3:?installer ISO output is required}"
iso_stage="$root/build/installer-iso-staging"

[[ "$stage" == "$root/build/installer-staging" ]] || {
    echo "Refusing unexpected installer staging path: $stage" >&2
    exit 1
}
[[ "$esp_image" == "$root/build/images/KuroganeOS-installer-esp.img" ]] || {
    echo "Refusing unexpected installer ESP path: $esp_image" >&2
    exit 1
}
[[ "$iso_image" == "$root/build/images/KuroganeOS-installer.iso" ]] || {
    echo "Refusing unexpected installer ISO path: $iso_image" >&2
    exit 1
}
command -v xorriso >/dev/null 2>&1 || {
    echo "xorriso is required" >&2
    exit 1
}
[[ -f "$stage/EFI/BOOT/BOOTX64.EFI" && -f "$stage/kernel.elf" &&
   -f "$stage/install.pkg" && -f "$esp_image" ]] || {
    echo "Installer staging is incomplete" >&2
    exit 1
}

rm -rf -- "$iso_stage"
mkdir -p "$iso_stage"
cp "$esp_image" "$iso_stage/efiboot.img"
cp -R "$stage/EFI" "$iso_stage/EFI"
cp "$stage/kernel.elf" "$iso_stage/kernel.elf"
cp "$stage/install.pkg" "$iso_stage/install.pkg"
rm -f -- "$iso_image"
xorriso -as mkisofs \
    -R -J -V KURO_INSTALL \
    -o "$iso_image" \
    -e efiboot.img \
    -no-emul-boot \
    -isohybrid-gpt-basdat \
    "$iso_stage"
echo "Built $iso_image"

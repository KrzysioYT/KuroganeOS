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
for tool in xorriso mdir mcopy python3; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "required ISO tool is unavailable: $tool" >&2
        exit 1
    }
done
[[ -f "$stage/EFI/BOOT/BOOTX64.EFI" && -f "$stage/kernel.elf" &&
   -f "$stage/install.pkg" && -f "$esp_image" ]] || {
    echo "Installer staging is incomplete" >&2
    exit 1
}

# The VirtualBox optical image is deliberately NOT the QEMU disk image.  The
# boot payload is a bounded FAT EFI System Partition containing the standard
# removable-media fallback path /EFI/BOOT/BOOTX64.EFI.
mdir -i "$esp_image" ::/EFI/BOOT/BOOTX64.EFI >/dev/null
mdir -i "$esp_image" ::/EFI/BOOT/kernel.elf >/dev/null
mdir -i "$esp_image" ::/kernel.elf >/dev/null
mdir -i "$esp_image" ::/install.pkg >/dev/null

rm -rf -- "$iso_stage"
mkdir -p "$iso_stage"
# Keep a visible copy for offline inspection. The actual El Torito boot entry
# below points at the appended GPT ESP, not at this ISO9660 file copy.
cp "$esp_image" "$iso_stage/efiboot.img"
cp -R "$stage/EFI" "$iso_stage/EFI"
cp "$stage/kernel.elf" "$iso_stage/kernel.elf"
cp "$stage/install.pkg" "$iso_stage/install.pkg"
rm -f -- "$iso_image"

# VirtualBox-targeted x86-64 UEFI optical layout.
#
# The ESP is appended outside the ISO9660 filesystem and represented as a real
# GPT EFI partition. El Torito platform 0xEF points directly at that appended
# partition. This avoids overlapping/nested GPT layouts and the historical
# isohybrid-gpt-basdat trick. It is intentionally UEFI-only: there is no BIOS
# boot entry because KuroganeOS has no legacy BIOS boot path.
#
# This layout follows xorriso's recommended pure-EFI recipe:
#   -append_partition 2 0xef ESP
#   -appended_part_as_gpt
#   -e --interval:appended_partition_2:all::
#   -no-emul-boot
xorriso -as mkisofs \
    -iso-level 3 \
    -R -J -V KURO_VBOX \
    -o "$iso_image" \
    -append_partition 2 0xef "$esp_image" \
    -appended_part_as_gpt \
    -no-pad \
    -c boot.catalog \
    -e --interval:appended_partition_2:all:: \
    -no-emul-boot \
    "$iso_stage"

[[ -s "$iso_image" ]] || {
    echo "ISO builder produced an empty image" >&2
    exit 1
}

# Publication gate: the image must expose a GPT ESP, an EFI El Torito entry and
# a valid AMD64 EFI application. The Windows media wrapper adds a mandatory real
# Oracle VirtualBox boot smoke before the canonical dist ISO is considered
# qualified.
bash "$root/scripts/verify-virtualbox-iso.sh" "$iso_image" --passes 20

echo "Built VirtualBox-targeted UEFI ISO: $iso_image"

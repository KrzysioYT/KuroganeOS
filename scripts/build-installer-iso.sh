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

# Verify the EFI boot filesystem itself before embedding it in El Torito.
mdir -i "$esp_image" ::/EFI/BOOT/BOOTX64.EFI >/dev/null
mdir -i "$esp_image" ::/EFI/BOOT/kernel.elf >/dev/null
mdir -i "$esp_image" ::/kernel.elf >/dev/null
mdir -i "$esp_image" ::/install.pkg >/dev/null

rm -rf -- "$iso_stage"
mkdir -p "$iso_stage"
cp "$esp_image" "$iso_stage/efiboot.img"
cp -R "$stage/EFI" "$iso_stage/EFI"
cp "$stage/kernel.elf" "$iso_stage/kernel.elf"
cp "$stage/install.pkg" "$iso_stage/install.pkg"
rm -f -- "$iso_image"

# Pure x86-64 UEFI media:
# 1. El Torito platform 0xEF -> efiboot.img for optical firmware (VirtualBox).
# 2. no-emulation mode, as required for EFI boot images.
# 3. expose that same EFI image as a proper GPT EFI System Partition rather
#    than the historical ISOLINUX isohybrid-gpt-basdat compatibility hack.
xorriso -as mkisofs \
    -R -J -V KURO_INSTALL \
    -o "$iso_image" \
    -c boot.catalog \
    -eltorito-platform efi \
    -e efiboot.img \
    -no-emul-boot \
    -efi-boot-part --efi-boot-image \
    "$iso_stage"

[[ -s "$iso_image" ]] || {
    echo "ISO builder produced an empty image" >&2
    exit 1
}

# Publication gate: do not let any host-specific wrapper copy an ISO to dist/
# until its El Torito entry, FAT boot filesystem, GPT ESP and AMD64 EFI loader
# have survived twenty complete independent inspections.
bash "$root/scripts/verify-virtualbox-iso.sh" "$iso_image" --passes 20

echo "Built and VirtualBox-verified $iso_image"

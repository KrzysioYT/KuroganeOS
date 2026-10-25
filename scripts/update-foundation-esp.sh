#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: update-foundation-esp.sh IMAGE EFI KERNEL" >&2
    exit 2
}

[[ $# -eq 3 ]] || usage
image="$1"
efi="$2"
kernel="$3"
[[ -f "$image" ]] || { echo "working image is missing: $image" >&2; exit 1; }
[[ -f "$efi" ]] || { echo "EFI loader is missing: $efi" >&2; exit 1; }
[[ -f "$kernel" ]] || { echo "kernel is missing: $kernel" >&2; exit 1; }

for tool in fsck.fat grep mcopy mdir sgdisk stat; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "required ESP update tool is unavailable: $tool" >&2
        exit 1
    }
done

[[ "$(stat -c %s "$image")" == 536870912 ]] || {
    echo "working image is not the expected 512 MiB Foundation disk" >&2
    exit 1
}
verification="$(sgdisk --verify "$image" 2>&1)"
grep -q 'No problems found' <<<"$verification" || {
    printf '%s\n' "$verification" >&2
    exit 1
}
partition="$(sgdisk --info=1 "$image")"
grep -q 'First sector: 2048 ' <<<"$partition" || {
    echo "working image ESP has an unexpected start LBA" >&2
    exit 1
}
grep -q 'Last sector: 133119 ' <<<"$partition" || {
    echo "working image ESP has an unexpected end LBA" >&2
    exit 1
}
grep -q 'EFI system partition' <<<"$partition" || {
    echo "working image partition 1 is not an ESP" >&2
    exit 1
}

export SOURCE_DATE_EPOCH=1767225600
export TZ=UTC
esp="$image@@$((2048 * 512))"
mdir -i "$esp" ::/EFI/BOOT >/dev/null
mcopy -o -m -i "$esp" "$efi" ::/EFI/BOOT/BOOTX64.EFI
mcopy -o -m -i "$esp" "$kernel" ::/kernel.elf
mcopy -o -m -i "$esp" "$kernel" ::/EFI/BOOT/kernel.elf

# Read back both payloads and validate the FAT metadata without modifying it.
temporary="$(mktemp -d)"
trap 'rm -rf -- "$temporary"' EXIT
mcopy -i "$esp" ::/EFI/BOOT/BOOTX64.EFI "$temporary/BOOTX64.EFI"
mcopy -i "$esp" ::/kernel.elf "$temporary/kernel.elf"
cmp -s "$efi" "$temporary/BOOTX64.EFI"
cmp -s "$kernel" "$temporary/kernel.elf"
dd if="$image" of="$temporary/esp.img" bs=1M skip=1 count=64 status=none
fsck.fat -vn "$temporary/esp.img" >/dev/null
echo "Updated working-image ESP; root partition was not written: $image"

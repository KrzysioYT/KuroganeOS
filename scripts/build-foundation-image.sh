#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat >&2 <<'EOF'
usage: build-foundation-image.sh --output FILE --efi FILE --kernel FILE [--rootfs DIR] [--overlay DIR]
EOF
    exit 2
}

output=""
efi=""
kernel=""
rootfs=""
overlay=""
while (($#)); do
    case "$1" in
        --output) [[ $# -ge 2 ]] || usage; output="$2"; shift 2 ;;
        --efi) [[ $# -ge 2 ]] || usage; efi="$2"; shift 2 ;;
        --kernel) [[ $# -ge 2 ]] || usage; kernel="$2"; shift 2 ;;
        --rootfs) [[ $# -ge 2 ]] || usage; rootfs="$2"; shift 2 ;;
        --overlay) [[ $# -ge 2 ]] || usage; overlay="$2"; shift 2 ;;
        *) usage ;;
    esac
done

[[ -n "$output" && -n "$efi" && -n "$kernel" ]] || usage
[[ -f "$efi" ]] || { echo "missing EFI loader: $efi" >&2; exit 1; }
[[ -f "$kernel" ]] || { echo "missing kernel: $kernel" >&2; exit 1; }
if [[ -n "$rootfs" && ! -d "$rootfs" ]]; then
    echo "missing rootfs staging directory: $rootfs" >&2
    exit 1
fi
if [[ -n "$overlay" && ! -d "$overlay" ]]; then
    echo "missing rootfs overlay directory: $overlay" >&2
    exit 1
fi

for tool in truncate sgdisk mkfs.fat mmd mcopy sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "required image tool is unavailable: $tool" >&2
        exit 1
    }
done

# Fixed layout: 512 MiB, 512-byte logical sectors, 1 MiB alignment.
readonly total_bytes=$((512 * 1024 * 1024))
readonly esp_first=2048
readonly esp_last=133119
readonly root_first=133120
readonly root_last=1046527
readonly esp_sectors=$((esp_last - esp_first + 1))
readonly root_sectors=$((root_last - root_first + 1))
readonly esp_bytes=$((esp_sectors * 512))
readonly root_bytes=$((root_sectors * 512))
readonly disk_guid="4B55524F-4741-4E45-8F44-49534B000001"
readonly esp_guid="4B55524F-4741-4E45-8F45-535000000001"
readonly root_guid="4B55524F-4741-4E45-8F52-4F4F54000001"
readonly root_type="4B55524F-4741-4E45-8F53-524F4F543001"

output_dir="$(dirname "$output")"
mkdir -p "$output_dir"
# mtools can block for minutes when repeatedly updating a large sparse image
# through WSL's /mnt filesystem. Build on the native WSL filesystem and copy
# the completed artifact once.
temporary_dir="$(mktemp -d)"
temporary="$temporary_dir/KuroganeOS-base.img"
esp_volume="$temporary_dir/esp.img"
root_volume="$temporary_dir/root.img"
publish_temp="$output_dir/.KuroganeOS-base.$$.tmp"
cleanup() {
    rm -rf -- "$temporary_dir"
    rm -f -- "$publish_temp"
}
trap cleanup EXIT

truncate -s "$total_bytes" "$temporary"
sgdisk --clear \
    --disk-guid="$disk_guid" \
    --new=1:"$esp_first":"$esp_last" \
    --typecode=1:EF00 \
    --partition-guid=1:"$esp_guid" \
    --change-name=1:"Kurogane ESP" \
    --new=2:"$root_first":"$root_last" \
    --typecode=2:"$root_type" \
    --partition-guid=2:"$root_guid" \
    --change-name=2:"Kurogane Root" \
    "$temporary" >/dev/null
sgdisk --verify "$temporary"

truncate -s "$esp_bytes" "$esp_volume"
truncate -s "$root_bytes" "$root_volume"
mkfs.fat --invariant -I -F 32 -S 512 -s 1 -h "$esp_first" \
    -i 4B455350 -n KURO_ESP "$esp_volume" >/dev/null
mkfs.fat --invariant -I -F 32 -S 512 -s 8 -h "$root_first" \
    -i 4B524F54 -n KURO_ROOT "$root_volume" >/dev/null

export SOURCE_DATE_EPOCH=1767225600
export TZ=UTC
esp_image="$esp_volume"
root_image="$root_volume"

mmd -i "$esp_image" ::/EFI ::/EFI/BOOT
mcopy -o -m -i "$esp_image" "$efi" ::/EFI/BOOT/BOOTX64.EFI
mcopy -o -m -i "$esp_image" "$kernel" ::/kernel.elf
mcopy -o -m -i "$esp_image" "$kernel" ::/EFI/BOOT/kernel.elf

mmd -i "$root_image" \
    ::/bin ::/boot ::/dev ::/etc ::/home ::/proc ::/system ::/tmp ::/var
mmd -i "$root_image" ::/home/user ::/system/bin ::/var/log
mcopy -o -m -i "$root_image" "$kernel" ::/boot/kernel.elf

copy_tree() {
    local source="$1"
    while IFS= read -r -d '' directory; do
        relative="${directory#"$source"/}"
        [[ "$relative" != "$directory" ]] || continue
        if ! mdir -i "$root_image" "::/$relative" >/dev/null 2>&1; then
            mmd -i "$root_image" "::/$relative"
        fi
    done < <(find "$source" -mindepth 1 -type d -print0 | sort -z)

    while IFS= read -r -d '' file; do
        relative="${file#"$source"/}"
        [[ "$relative" != "$file" ]] || continue
        parent="$(dirname "$relative")"
        if [[ "$parent" != "." ]] &&
           ! mdir -i "$root_image" "::/$parent" >/dev/null 2>&1; then
            mmd -i "$root_image" "::/$parent"
        fi
        mcopy -o -m -i "$root_image" "$file" "::/$relative"
    done < <(find "$source" -type f -print0 | sort -z)
}

if [[ -n "$rootfs" ]]; then
    copy_tree "$rootfs"
fi
if [[ -n "$overlay" ]]; then
    copy_tree "$overlay"
fi

fsck.fat -vn "$esp_volume" >/dev/null
fsck.fat -vn "$root_volume" >/dev/null
dd if="$esp_volume" of="$temporary" bs=1M seek=1 conv=notrunc status=none
dd if="$root_volume" of="$temporary" bs=1M seek=65 conv=notrunc status=none
sgdisk --verify "$temporary" >/dev/null

# Publish only after both filesystems and all staged files were written. The
# final rename is atomic within the output directory.
cp --sparse=always -- "$temporary" "$publish_temp"
mv -f -- "$publish_temp" "$output"
rm -rf -- "$temporary_dir"
trap - EXIT

echo "Created KuroganeOS Foundation GPT image: $output"
echo "  disk: 512 MiB, GUID $disk_guid"
echo "  ESP:  LBA $esp_first-$esp_last, FAT32 KURO_ESP"
echo "  root: LBA $root_first-$root_last, FAT32 KURO_ROOT"
echo "  SHA-256: $(sha256sum "$output" | awk '{print $1}')"

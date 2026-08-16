#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: ./scripts/build-installer-esp.sh STAGE OUTPUT" >&2
    exit 2
}

[[ $# -eq 2 ]] || usage
stage="$1"
output="$2"

for tool in python3 mkfs.fat mmd mcopy mdir; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "required EFI image tool missing: $tool" >&2
        exit 1
    }
done
if command -v fsck.fat >/dev/null 2>&1; then
    fsck_tool=fsck.fat
elif command -v dosfsck >/dev/null 2>&1; then
    fsck_tool=dosfsck
else
    echo "fsck.fat/dosfsck is required" >&2
    exit 1
fi

for path in \
    "$stage/EFI/BOOT/BOOTX64.EFI" \
    "$stage/EFI/BOOT/kernel.elf" \
    "$stage/kernel.elf" \
    "$stage/install.pkg"; do
    [[ -f "$path" && -s "$path" ]] || {
        echo "missing or empty installer staging file: $path" >&2
        exit 1
    }
done

# El Torito stores the boot-image sector count in 16 bits. xorriso documents
# EFI boot images as not larger than 65535 blocks of 512 bytes. The previous
# KuroganeOS image was 64 MiB and therefore exceeded that range. A 30 MiB
# FAT16 image is 61440 sectors: safely below the catalog limit while large
# enough for the <=16 MiB install package plus loader/kernel copies.
readonly bytes_per_sector=512
readonly image_bytes=$((30 * 1024 * 1024))
readonly sectors=$((image_bytes / bytes_per_sector))
((sectors < 65535)) || {
    echo "internal error: El Torito EFI image exceeds 65535 sectors" >&2
    exit 1
}

mkdir -p "$(dirname "$output")"
rm -f -- "$output"
python3 - "$output" "$image_bytes" <<'PY'
import sys
path=sys.argv[1]
size=int(sys.argv[2])
with open(path, 'wb') as image:
    image.truncate(size)
PY

mkfs.fat -F 16 -S 512 -n KUROESP "$output" >/dev/null
mmd -i "$output" ::/EFI
mmd -i "$output" ::/EFI/BOOT
mcopy -o -i "$output" "$stage/EFI/BOOT/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
mcopy -o -i "$output" "$stage/EFI/BOOT/kernel.elf" ::/EFI/BOOT/kernel.elf
mcopy -o -i "$output" "$stage/kernel.elf" ::/kernel.elf
mcopy -o -i "$output" "$stage/install.pkg" ::/install.pkg

"$fsck_tool" -n "$output" >/dev/null
mdir -i "$output" ::/EFI/BOOT/BOOTX64.EFI >/dev/null
mdir -i "$output" ::/EFI/BOOT/kernel.elf >/dev/null
mdir -i "$output" ::/kernel.elf >/dev/null
mdir -i "$output" ::/install.pkg >/dev/null

actual_bytes="$(wc -c < "$output" | tr -d ' ')"
[[ "$actual_bytes" == "$image_bytes" ]] || {
    echo "unexpected El Torito EFI image size: $actual_bytes" >&2
    exit 1
}

echo "[installer-esp] FAT16 UEFI image: $output"
echo "[installer-esp] bytes=$image_bytes sectors=$sectors (<65535 PASS)"

#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

usage() {
    echo "usage: build-foundation-image-macos.sh --output FILE --efi FILE --kernel FILE --rootfs DIR --overlay DIR" >&2
    exit 2
}

output=""; efi=""; kernel=""; rootfs=""; overlay=""
while (($#)); do
    case "$1" in
        --output) output="$2"; shift 2 ;;
        --efi) efi="$2"; shift 2 ;;
        --kernel) kernel="$2"; shift 2 ;;
        --rootfs) rootfs="$2"; shift 2 ;;
        --overlay) overlay="$2"; shift 2 ;;
        *) usage ;;
    esac
done
[[ -n "$output" && -n "$efi" && -n "$kernel" && -n "$rootfs" && -n "$overlay" ]] || usage
[[ -f "$efi" && -f "$kernel" && -d "$rootfs" && -d "$overlay" ]] || {
    echo "missing Foundation image input" >&2; exit 1; }

for tool in python3 sgdisk mkfs.fat fsck.fat mmd mcopy mdir dd; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "required image tool is unavailable: $tool" >&2; exit 1; }
done
host_cxx="${HOST_CXX:-c++}"
command -v "$host_cxx" >/dev/null 2>&1 || {
    echo "host C++ compiler is required for image validation: $host_cxx" >&2
    echo "install Apple Command Line Tools with: xcode-select --install" >&2
    exit 1
}

readonly total_bytes=$((512 * 1024 * 1024))
readonly esp_first=2048
readonly esp_last=133119
readonly root_first=133120
readonly root_last=1046527
readonly esp_bytes=$(((esp_last - esp_first + 1) * 512))
readonly root_bytes=$(((root_last - root_first + 1) * 512))
readonly disk_guid="4B55524F-4741-4E45-8F44-49534B000001"
readonly esp_guid="4B55524F-4741-4E45-8F45-535000000001"
readonly root_guid="4B55524F-4741-4E45-8F52-4F4F54000001"
readonly root_type="4B55524F-4741-4E45-8F53-524F4F543001"

tmp="$(mktemp -d "${TMPDIR:-/tmp}/kurogane-macos.XXXXXX")"
disk="$tmp/KuroganeOS.img"
esp="$tmp/esp.img"
root="$tmp/root.img"
root_test="$tmp/root-volume-image-test"
cleanup() { rm -rf -- "$tmp"; }
trap cleanup EXIT
mkdir -p "$(dirname "$output")"

python3 - "$disk" "$total_bytes" "$esp" "$esp_bytes" "$root" "$root_bytes" <<'PY'
import sys
for path, size in ((sys.argv[1], int(sys.argv[2])),
                   (sys.argv[3], int(sys.argv[4])),
                   (sys.argv[5], int(sys.argv[6]))):
    with open(path, "wb") as handle:
        handle.truncate(size)
PY

sgdisk --clear \
    --disk-guid="$disk_guid" \
    --new=1:"$esp_first":"$esp_last" --typecode=1:EF00 \
    --partition-guid=1:"$esp_guid" --change-name=1:"Kurogane ESP" \
    --new=2:"$root_first":"$root_last" --typecode=2:"$root_type" \
    --partition-guid=2:"$root_guid" --change-name=2:"Kurogane Root" \
    "$disk" >/dev/null
sgdisk --verify "$disk" >/dev/null

mkfs_args=()
if mkfs.fat --help 2>&1 | grep -q -- '--invariant'; then
    mkfs_args+=(--invariant)
fi
mkfs.fat "${mkfs_args[@]}" -I -F 32 -S 512 -s 1 -h "$esp_first" -i 4B455350 -n KURO_ESP "$esp" >/dev/null
mkfs.fat "${mkfs_args[@]}" -I -F 32 -S 512 -s 8 -h "$root_first" -i 4B524F54 -n KURO_ROOT "$root" >/dev/null

export SOURCE_DATE_EPOCH=1767225600
export TZ=UTC

mmd -i "$esp" ::/EFI ::/EFI/BOOT
mcopy -o -m -i "$esp" "$efi" ::/EFI/BOOT/BOOTX64.EFI
mcopy -o -m -i "$esp" "$kernel" ::/kernel.elf
mcopy -o -m -i "$esp" "$kernel" ::/EFI/BOOT/kernel.elf

mmd -i "$root" ::/bin ::/boot ::/dev ::/etc ::/home ::/proc ::/system ::/tmp ::/var
mmd -i "$root" ::/home/user ::/system/bin ::/var/log
mcopy -o -m -i "$root" "$kernel" ::/boot/kernel.elf

copy_tree() {
    local source="$1"
    while IFS= read -r -d '' directory; do
        local relative="${directory#"$source"/}"
        [[ "$relative" != "$directory" ]] || continue
        mdir -i "$root" "::/$relative" >/dev/null 2>&1 || mmd -i "$root" "::/$relative"
    done < <(find "$source" -mindepth 1 -type d -print0)
    while IFS= read -r -d '' file; do
        local relative="${file#"$source"/}"
        local parent
        parent="$(dirname "$relative")"
        if [[ "$parent" != "." ]]; then
            mdir -i "$root" "::/$parent" >/dev/null 2>&1 || mmd -i "$root" "::/$parent" 2>/dev/null || true
        fi
        mcopy -o -m -i "$root" "$file" "::/$relative"
    done < <(find "$source" -type f -print0)
}
copy_tree "$rootfs"
copy_tree "$overlay"

# mtools/dosfstools implementations can leave mirror/backup metadata in a form
# that is legal enough for host tools but rejected by KuroganeOS' deliberately
# strict FAT32 mount checks. Normalize only duplicated metadata; file data and
# directory entries are not rewritten.
python3 "$repo_root/scripts/normalize-fat32.py" "$esp"
python3 "$repo_root/scripts/normalize-fat32.py" "$root"

fsck.fat -vn "$esp" >/dev/null
fsck.fat -vn "$root" >/dev/null
# BSD dd has no status=none; silence its progress portably.
dd if="$esp" of="$disk" bs=1048576 seek=1 conv=notrunc 2>/dev/null
dd if="$root" of="$disk" bs=1048576 seek=65 conv=notrunc 2>/dev/null
sgdisk --verify "$disk" >/dev/null

# Do not publish a development image merely because host fsck accepts it.
# Compile the project's own FAT32/GPT/VFS host test and exercise the complete
# disk image through the exact filesystem implementation used by KuroganeOS.
echo "[macos] validating Foundation image through KuroganeOS FAT32/VFS..."
"$host_cxx" -std=c++17 -O2 -Wall -Wextra -Wpedantic \
    "$repo_root/tests/test_root_volume_image.cpp" \
    "$repo_root/kernel/fs/root_volume.cpp" \
    "$repo_root/kernel/fs/fat32.cpp" \
    "$repo_root/kernel/fs/fat32_vfs.cpp" \
    "$repo_root/kernel/fs/vfs.cpp" \
    "$repo_root/kernel/storage/gpt.cpp" \
    "$repo_root/kernel/storage/partition_device.cpp" \
    -o "$root_test"
"$root_test" "$disk"
echo "[macos] Foundation root FAT32/VFS validation: PASS"

cp "$disk" "$output"

hash="$(shasum -a 256 "$output" | awk '{print $1}')"
echo "Created KuroganeOS macOS Foundation GPT image: $output"
echo "  ESP:  LBA $esp_first-$esp_last, FAT32 KURO_ESP"
echo "  root: LBA $root_first-$root_last, FAT32 KURO_ROOT"
echo "  SHA-256: $hash"

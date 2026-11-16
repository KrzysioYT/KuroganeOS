#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image="${1:-}"
if [[ -z "$image" || ! -f "$image" ]]; then
    echo "usage: validate-foundation-root-image.sh IMAGE" >&2
    exit 2
fi

cxx="${HOST_CXX:-c++}"
command -v "$cxx" >/dev/null 2>&1 || {
    echo "host C++ compiler is unavailable: $cxx" >&2
    exit 1
}

temporary_dir="$(mktemp -d "${TMPDIR:-/tmp}/kurogane-root-image-test.XXXXXX")"
trap 'rm -rf -- "$temporary_dir"' EXIT

"$cxx" -std=c++17 -O2 -Wall -Wextra -Wpedantic \
    "$root/tests/test_root_volume_image.cpp" \
    "$root/tests/host_system_metrics_stub.cpp" \
    "$root/tests/host_process_path_stub.cpp" \
    "$root/kernel/fs/root_volume.cpp" \
    "$root/kernel/fs/fat32.cpp" \
    "$root/kernel/fs/fat32_vfs.cpp" \
    "$root/kernel/fs/vfs.cpp" \
    "$root/kernel/install/package.cpp" \
    "$root/kernel/install/package_vfs.cpp" \
    "$root/kernel/libk/crc.cpp" \
    "$root/kernel/storage/gpt.cpp" \
    "$root/kernel/storage/partition_device.cpp" \
    -o "$temporary_dir/test_root_volume_image"

"$temporary_dir/test_root_volume_image" "$image"

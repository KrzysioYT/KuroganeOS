#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sysroot="$root/build/sdk/sysroot"
include_source="$root/sdk/include"
example_source="$root/sdk/examples/abi-inspect/main.cpp"
example_output="$root/build/sdk/examples/abi-inspect.o"
cxx="${CXX:-g++}"

case "$sysroot" in
    "$root"/build/sdk/sysroot) ;;
    *) echo "refusing unexpected sysroot path: $sysroot" >&2; exit 1 ;;
esac

rm -rf -- "$sysroot"
mkdir -p "$sysroot/usr/include/kurogane" "$(dirname "$example_output")"
cp "$include_source"/kurogane/*.h "$sysroot/usr/include/kurogane/"

"$cxx" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
    -ffreestanding -I"$sysroot/usr/include" \
    -c "$example_source" -o "$example_output"

printf 'Built experimental SDK sysroot: %s\n' "$sysroot"
printf 'Compiled external ABI consumer: %s\n' "$example_output"

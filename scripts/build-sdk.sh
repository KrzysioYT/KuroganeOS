#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
host_os="$(uname -s)"
host_arch="$(uname -m)"

if command -v x86_64-elf-gcc >/dev/null 2>&1; then
    default_cc=x86_64-elf-gcc
    default_cxx=x86_64-elf-g++
    default_ar=x86_64-elf-ar
    default_readelf=x86_64-elf-readelf
elif [[ "$host_os" == Linux && "$host_arch" == x86_64 ]] && \
     command -v gcc >/dev/null 2>&1 && command -v g++ >/dev/null 2>&1; then
    default_cc=gcc
    default_cxx=g++
    default_ar=ar
    default_readelf=readelf
elif command -v powershell.exe >/dev/null 2>&1 && command -v wslpath >/dev/null 2>&1; then
    exec powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass \
        -File "$(wslpath -w "$root/scripts/build-sdk.ps1")"
else
    echo "KuroganeOS SDK needs x86_64-elf-gcc, or native x86_64 Linux gcc/g++." >&2
    echo "macOS: ./scripts/setup-macos.sh --install" >&2
    echo "Linux: ./scripts/setup-linux.sh --install" >&2
    exit 1
fi

cc="${CC:-$default_cc}"
cxx="${CXX:-$default_cxx}"
ar="${AR:-$default_ar}"
readelf="${READELF:-$default_readelf}"
for tool in "$cc" "$cxx" "$ar" "$readelf"; do
    command -v "$tool" >/dev/null 2>&1 || { echo "missing SDK tool: $tool" >&2; exit 1; }
done

build="$root/build/sdk"
sysroot="$build/sysroot"
obj="$build/obj"
lib="$sysroot/usr/lib"
include="$sysroot/usr/include"
examples="$build/examples"
overlay_apps="$root/build/userspace/rootfs/apps"
overlay_gui="$root/build/userspace/rootfs/gui"

rm -rf -- "$sysroot" "$obj" "$examples"
mkdir -p "$include" "$lib" "$obj" "$examples" "$overlay_apps" "$overlay_gui"
cp -R "$root/sdk/include/." "$include/"
cp "$root/userspace/linker.ld" "$lib/kurogane-user.ld"

common=(
    -ffreestanding -fno-stack-protector -m64 -mno-red-zone
    -mno-mmx -mno-sse -msoft-float -fno-pic -fno-pie
    -mcmodel=large -fno-builtin -ffunction-sections -fdata-sections
    -Wa,--noexecstack -O2 -Wall -Wextra -Wpedantic -Werror
    -I "$include" -I "$root/common"
)

"$cc" "${common[@]}" -c -x assembler-with-cpp "$root/sdk/src/crt0.S" -o "$obj/crt0.o"
"$cc" "${common[@]}" -std=c11 -c "$root/sdk/src/libc.c" -o "$obj/libc.o"
"$cc" "${common[@]}" -std=c11 -c "$root/sdk/src/libkurogane.c" -o "$obj/libkurogane.o"
"$cc" "${common[@]}" -std=c11 -c "$root/sdk/src/libui.c" -o "$obj/libui.o"
cp "$obj/crt0.o" "$lib/crt0.o"
"$ar" rcs "$lib/libc.a" "$obj/libc.o"
"$ar" rcs "$lib/libkurogane.a" "$obj/libkurogane.o"
"$ar" rcs "$lib/libui.a" "$obj/libui.o"

validate_elf() {
    local elf="$1"
    local label="$2"
    "$readelf" -hW "$elf" | grep -Eq 'Type:[[:space:]]+EXEC' || {
        echo "$label is not ET_EXEC" >&2; return 1; }
    "$readelf" -hW "$elf" | grep -Eq 'Machine:[[:space:]]+Advanced Micro Devices X86-64' || {
        echo "$label is not x86-64" >&2; return 1; }
    if "$readelf" -lW "$elf" | grep -Eq '^[[:space:]]*(LOAD|GNU_STACK).*RWE'; then
        echo "$label has an executable writable segment or stack" >&2
        return 1
    fi
    if "$readelf" -sW "$elf" | grep -Eq '^[[:space:]]*[1-9][0-9]*:.*[[:space:]]UND[[:space:]]'; then
        echo "$label has undefined symbols" >&2
        return 1
    fi
}

example_obj="$examples/hello.o"
example_elf="$examples/hello"
"$cxx" "${common[@]}" -std=c++17 -fno-exceptions -fno-rtti \
    -c "$root/sdk/examples/hello/main.cpp" -o "$example_obj"
"$cxx" -nostdlib -static -no-pie \
    -Wl,--fatal-warnings -Wl,--build-id=none -Wl,-z,noexecstack \
    -Wl,-z,separate-code -Wl,--gc-sections \
    -T "$lib/kurogane-user.ld" -o "$example_elf" \
    "$lib/crt0.o" "$example_obj" -L "$lib" \
    -Wl,--start-group -lc -lkurogane -lui -lgcc -Wl,--end-group
validate_elf "$example_elf" "external SDK example"
cp "$example_elf" "$overlay_apps/external"

declare -a gui_names=(login launcher terminal files sysmon performance browser about settings)
for name in "${gui_names[@]}"; do
    source="$root/userspace/gui/$name/main.c"
    object="$obj/gui-$name.o"
    elf="$overlay_gui/$name"
    "$cc" "${common[@]}" -std=c11 -I "$root/userspace/gui" -c "$source" -o "$object"
    "$cc" -nostdlib -static -no-pie \
        -Wl,--fatal-warnings -Wl,--build-id=none -Wl,-z,noexecstack \
        -Wl,-z,separate-code -Wl,--gc-sections \
        -T "$lib/kurogane-user.ld" -o "$elf" \
        "$lib/crt0.o" "$object" -L "$lib" \
        -Wl,--start-group -lui -lc -lkurogane -lgcc -Wl,--end-group
    validate_elf "$elf" "desktop SDK ELF $name"
    echo "[sdk] /gui/$name"
done

echo "[sdk] sysroot: $sysroot"
echo "[sdk] external ELF: $example_elf"
echo "[sdk] crt0 + libc + libkurogane + libui + desktop apps: PASS"

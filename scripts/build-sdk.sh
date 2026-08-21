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

if [[ "$host_os" == Darwin ]]; then
    cc="${KUROGANE_CC:-$default_cc}"
    cxx="${KUROGANE_CXX:-$default_cxx}"
    ar="${KUROGANE_AR:-$default_ar}"
    readelf="${KUROGANE_READELF:-$default_readelf}"
else
    cc="${KUROGANE_CC:-${CC:-$default_cc}}"
    cxx="${KUROGANE_CXX:-${CXX:-$default_cxx}}"
    ar="${KUROGANE_AR:-${AR:-$default_ar}}"
    readelf="${KUROGANE_READELF:-${READELF:-$default_readelf}}"
fi
for tool in "$cc" "$cxx" "$ar" "$readelf"; do
    command -v "$tool" >/dev/null 2>&1 || { echo "missing SDK tool: $tool" >&2; exit 1; }
done

if [[ "$host_os" == Darwin ]]; then
    probe_dir="$(mktemp -d "${TMPDIR:-/tmp}/kurogane-sdk-probe.XXXXXX")"
    trap 'rm -rf -- "$probe_dir"' EXIT
    printf '%s\n' 'void kurogane_sdk_c_probe(void) {}' > "$probe_dir/probe.c"
    printf '%s\n' 'void kurogane_sdk_cxx_probe() {}' > "$probe_dir/probe.cpp"
    "$cc" -ffreestanding -fno-stack-protector -m64 -c \
        "$probe_dir/probe.c" -o "$probe_dir/probe-c.o"
    "$cxx" -ffreestanding -fno-stack-protector -m64 -fno-exceptions -fno-rtti -c \
        "$probe_dir/probe.cpp" -o "$probe_dir/probe-cxx.o"
    for probe in "$probe_dir/probe-c.o" "$probe_dir/probe-cxx.o"; do
        if ! "$readelf" -hW "$probe" 2>/dev/null | \
            grep -Eq 'Machine:[[:space:]]+Advanced Micro Devices X86-64'; then
            echo "[sdk] invalid macOS compiler target; expected x86-64 ELF" >&2
            echo "[sdk] C compiler: $cc" >&2
            echo "[sdk] C++ compiler: $cxx" >&2
            echo "[sdk] Apple clang/clang++ cannot be used for KuroganeOS target binaries" >&2
            exit 1
        fi
    done
    rm -rf -- "$probe_dir"
    trap - EXIT
    echo "[sdk] macOS target toolchain verified: x86-64 ELF"
fi

build="$root/build/sdk"
sysroot="$build/sysroot"
obj="$build/obj"
lib="$sysroot/usr/lib"
include="$sysroot/usr/include"
examples="$build/examples"
overlay_root="$root/build/userspace/rootfs"
overlay_apps="$overlay_root/apps"
overlay_gui="$overlay_root/gui"
overlay_etc="$overlay_root/etc"

rm -rf -- "$sysroot" "$obj" "$examples"
mkdir -p "$include" "$lib" "$obj" "$examples" "$overlay_apps" "$overlay_gui" "$overlay_etc"

header_source="$root/sdk/include"
while IFS= read -r -d '' directory; do
    relative="${directory#"$header_source"}"
    mkdir -p "$include$relative"
done < <(find "$header_source" -type d -print0)
while IFS= read -r -d '' header; do
    relative="${header#"$header_source"/}"
    cp "$header" "$include/$relative"
done < <(find "$header_source" -type f -print0)
for required_header in \
    stdlib.h string.h kurogane/kurogane.h kurogane/graphics.h kurogane/test_host.h; do
    [[ -f "$include/$required_header" ]] || {
        echo "[sdk] sysroot header staging failed: $required_header" >&2
        exit 1
    }
done

cp "$root/userspace/linker.ld" "$lib/kurogane-user.ld"

anvil_repo_host="${ANVIL_REPO_HOST:-raw.githubusercontent.com}"
anvil_repo_base="${ANVIL_REPO_BASE:-/KrzysioYT/KuroganeOS-Packages/main}"
cat > "$overlay_etc/anvil.cfg" <<EOF
HOST=$anvil_repo_host
BASE=$anvil_repo_base
EOF

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

declare -a gui_specs=(
    login:login
    launcher:launcher
    terminal:terminal
    files:files
    anvil:anvil
    pulse:pulse
    sysmon:sysmon
    performance:perf
    browser:browser
    about:about
    settings:settings
)
for spec in "${gui_specs[@]}"; do
    source_name="${spec%%:*}"
    install_name="${spec##*:}"
    source="$root/userspace/gui/$source_name/main.c"
    object="$obj/gui-$source_name.o"
    elf="$overlay_gui/$install_name"
    "$cc" "${common[@]}" -std=c11 -I "$root/userspace/gui" -c "$source" -o "$object"
    "$cc" -nostdlib -static -no-pie \
        -Wl,--fatal-warnings -Wl,--build-id=none -Wl,-z,noexecstack \
        -Wl,-z,separate-code -Wl,--gc-sections \
        -T "$lib/kurogane-user.ld" -o "$elf" \
        "$lib/crt0.o" "$object" -L "$lib" \
        -Wl,--start-group -lui -lc -lkurogane -lgcc -Wl,--end-group
    validate_elf "$elf" "desktop SDK ELF $source_name"
    echo "[sdk] /gui/$install_name"
done

echo "[sdk] Anvil repo: https://$anvil_repo_host$anvil_repo_base/index.kuro"
echo "[sdk] sysroot: $sysroot"
echo "[sdk] external ELF: $example_elf"
echo "[sdk] crt0 + libc + libkurogane + libui + desktop apps: PASS"

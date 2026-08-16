#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
usage() {
    echo "usage: ./scripts/build-app-macos.sh SOURCE [-o NAME] [--install]" >&2
    exit 2
}
[[ $# -ge 1 ]] || usage
source="$1"; shift
name="$(basename "$source")"; name="${name%.*}"
install=false
while (($#)); do
    case "$1" in
        -o|--output) name="${2:-}"; shift 2 ;;
        --install) install=true; shift ;;
        *) usage ;;
    esac
done
[[ -f "$source" ]] || { echo "source file not found: $source" >&2; exit 1; }
[[ "$name" =~ ^[A-Za-z0-9._-]+$ ]] || { echo "invalid app name: $name" >&2; exit 1; }

if [[ ! -f "$root/build/sdk/sysroot/usr/lib/crt0.o" ]]; then
    "$root/scripts/build-sdk.sh"
fi

cc=x86_64-elf-gcc
cxx=x86_64-elf-g++
readelf=x86_64-elf-readelf
for tool in "$cc" "$cxx" "$readelf"; do
    command -v "$tool" >/dev/null 2>&1 || { echo "missing tool: $tool" >&2; exit 1; }
done

sysroot="$root/build/sdk/sysroot"
lib="$sysroot/usr/lib"
include="$sysroot/usr/include"
outdir="$root/build/apps"
mkdir -p "$outdir"
object="$outdir/$name.o"
elf="$outdir/$name"
common=(-ffreestanding -fno-stack-protector -m64 -mno-red-zone -mno-mmx -mno-sse \
    -msoft-float -fno-pic -fno-pie -mcmodel=large -fno-builtin \
    -ffunction-sections -fdata-sections -Wa,--noexecstack -O2 \
    -Wall -Wextra -Wpedantic -Werror -I "$include")

case "$source" in
    *.cpp|*.cc|*.cxx)
        compiler="$cxx"
        "$compiler" "${common[@]}" -std=c++17 -fno-exceptions -fno-rtti -c "$source" -o "$object"
        ;;
    *.c)
        compiler="$cc"
        "$compiler" "${common[@]}" -std=c11 -c "$source" -o "$object"
        ;;
    *) echo "supported sources: .c, .cpp, .cc, .cxx" >&2; exit 1 ;;
esac

"$compiler" -nostdlib -static -no-pie \
    -Wl,--fatal-warnings -Wl,--build-id=none -Wl,-z,noexecstack \
    -Wl,-z,separate-code -Wl,--gc-sections \
    -T "$lib/kurogane-user.ld" -o "$elf" "$lib/crt0.o" "$object" \
    -L "$lib" -Wl,--start-group -lc -lkurogane -lui -lgcc -Wl,--end-group

"$readelf" -hW "$elf" | grep -Eq 'Type:[[:space:]]+EXEC' || { echo "app is not ET_EXEC" >&2; exit 1; }
if "$readelf" -lW "$elf" | grep -Eq '^[[:space:]]*(LOAD|GNU_STACK).*RWE'; then
    echo "app has an executable writable segment/stack" >&2; exit 1
fi

if $install; then
    mkdir -p "$root/build/userspace/rootfs/apps"
    cp "$elf" "$root/build/userspace/rootfs/apps/$name"
    echo "[app] installed into development rootfs as /apps/$name"
fi

echo "[app] built: $elf"

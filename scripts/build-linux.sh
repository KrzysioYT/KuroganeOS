#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

configuration=debug
stage_only=false
clean=false
rebuild=false
no_image=false

usage() {
    cat >&2 <<'USAGE'
usage: bash ./scripts/build-linux.sh [options]
  --configuration debug|release|test
  --clean
  --rebuild
  --stage-only
  --no-image
USAGE
    exit 2
}

while (($#)); do
    case "$1" in
        --configuration) configuration="${2:-}"; shift 2 ;;
        --clean) clean=true; shift ;;
        --rebuild) rebuild=true; shift ;;
        --stage-only) stage_only=true; shift ;;
        --no-image) no_image=true; shift ;;
        *) usage ;;
    esac
done
case "$configuration" in debug|release|test) ;; *) usage ;; esac

[[ "$(uname -s)" == Linux ]] || { echo "build-linux.sh requires Linux" >&2; exit 1; }
[[ "$(uname -m)" == x86_64 ]] || {
    echo "Linux build currently requires x86_64 or explicit x86_64-elf tools." >&2
    exit 1
}

if command -v x86_64-elf-gcc >/dev/null 2>&1; then
    target_prefix="x86_64-elf-"
else
    target_prefix=""
fi
cc="${CC:-${target_prefix}gcc}"
cxx="${CXX:-${target_prefix}g++}"
ld="${LD:-${target_prefix}ld}"
objcopy="${OBJCOPY:-${target_prefix}objcopy}"
objdump="${OBJDUMP:-${target_prefix}objdump}"
readelf="${READELF:-${target_prefix}readelf}"
for tool in "$cc" "$cxx" "$ld" "$objcopy" "$objdump" "$readelf" python3 make; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "missing build tool: $tool" >&2
        echo "run: bash ./scripts/setup-linux.sh --install" >&2
        exit 1
    }
done

clean_outputs() {
    rm -rf -- build/obj build/boot build/userspace build/sdk build/images
    rm -f -- build/kernel.elf build/kernel.map build/BOOTX64.EFI build/build-info.txt
    rm -rf -- build/installer-staging build/installer-iso-staging
    rm -f -- build/install.pkg
    rm -rf -- iso/EFI
    rm -f -- iso/kernel.elf
    rm -f -- dist/KuroganeOS-*-linux-qemu.img dist/KuroganeOS-*-linux-qemu.img.sha256
    echo "[linux] build outputs cleaned"
}

if $clean; then clean_outputs; exit 0; fi
if $rebuild; then clean_outputs; fi

mkdir -p build build/userspace/rootfs build/boot iso/EFI/BOOT dist

if ! $stage_only; then
    echo "[linux] building x86-64 kernel ($configuration)"
    make CONFIG="$configuration" TARGET_PREFIX="$target_prefix" EXEEXT= kernel
elif [[ ! -f build/kernel.elf ]]; then
    echo "--stage-only requires build/kernel.elf" >&2
    exit 1
fi

rm -rf build/userspace/rootfs
mkdir -p build/userspace/rootfs/apps build/userspace/rootfs/system
applications=(
    "hello|userspace/apps/hello/main.S|apps/hello|asm"
    "bad|userspace/apps/bad/main.S|apps/bad|asm"
    "spin|userspace/apps/spin/main.S|apps/spin|asm"
    "probe|userspace/apps/probe/main.S|apps/probe|asm"
    "shell|userspace/apps/shell/main.c|apps/shell|c"
    "files|userspace/apps/files/main.c|apps/files|c"
    "monitor|userspace/apps/monitor/main.c|apps/monitor|c"
    "about|userspace/apps/about/main.c|apps/about|c"
    "init|userspace/system/init/main.c|system/init|c"
    "eventd|userspace/system/eventd/main.c|system/eventd|c"
    "evprobe|userspace/system/event-probe/main.c|system/evprobe|c"
    "settingsd|userspace/system/settingsd/main.c|system/setd|c"
    "setprobe|userspace/system/settings-probe/main.c|system/setprobe|c"
    "notificationd|userspace/system/notificationd/main.c|system/notifd|c"
    "notifprobe|userspace/system/notification-probe/main.c|system/ntfprobe|c"
    "accountd|userspace/system/accountd/main.c|system/accountd|c"
    "accountprobe|userspace/system/account-probe/main.c|system/acctprb|c"
    "sessiond|userspace/system/sessiond/main.c|system/sessiond|c"
    "sessionprobe|userspace/system/session-probe/main.c|system/sesprobe|c"
    "settingschangeprobe|userspace/system/settings-change-probe/main.c|system/setchprb|c"
    "clipboardd|userspace/system/clipboardd/main.c|system/clipd|c"
    "networkeventd|userspace/system/network-eventd/main.c|system/neteventd|c"
    "appregistryd|userspace/system/app-registryd/main.c|system/appregd|c"
    "clipboardprobe|userspace/system/clipboard-probe/main.c|system/clipprb|c"
)
for spec in "${applications[@]}"; do
    IFS='|' read -r name source output kind <<<"$spec"
    object="build/userspace/$name.o"
    target="build/userspace/rootfs/$output"
    mkdir -p "$(dirname "$target")"
    common=(-ffreestanding -fno-stack-protector -m64 -mno-red-zone \
        -mno-mmx -mno-sse -msoft-float -Wa,--noexecstack \
        -I sdk/include -I userspace/runtime -I common)
    if [[ "$kind" == asm ]]; then
        "$cc" "${common[@]}" -c -x assembler-with-cpp "$source" -o "$object"
    else
        "$cc" "${common[@]}" -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
            -fno-builtin -fno-pic -fno-pie -mcmodel=large -c "$source" -o "$object"
    fi
    "$ld" --fatal-warnings --build-id=none -nostdlib -z noexecstack -z separate-code \
        -T userspace/linker.ld -o "$target" "$object"
    "$readelf" -hW "$target" | grep -Eq 'Type:[[:space:]]+EXEC' || exit 1
    "$readelf" -hW "$target" | grep -Eq 'Machine:[[:space:]]+Advanced Micro Devices X86-64' || exit 1
    if "$readelf" -lW "$target" | grep -Eq '^[[:space:]]*(LOAD|GNU_STACK).*RWE'; then
        echo "userspace image has W+X segment/stack: $name" >&2; exit 1
    fi
    if "$objdump" -d "$target" | grep -Eq '%(xmm|ymm|zmm|mm)[0-9]+'; then
        echo "userspace image uses unsupported SIMD/FPU register state: $name" >&2
        exit 1
    fi
    echo "[userspace] /$output"
done

CC="$cc" CXX="$cxx" AR="${AR:-${target_prefix}ar}" READELF="$readelf" \
    bash "$root/scripts/build-sdk.sh"

"$cc" -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
    -ffreestanding -fshort-wchar -m64 -mno-red-zone -mno-mmx -mno-sse \
    -fno-stack-protector -fno-omit-frame-pointer -fPIE -fno-plt -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -ffunction-sections -fdata-sections -Wa,--noexecstack \
    -I boot/efi -I boot/include -I common -frandom-seed=boot/efi/standalone.c \
    -c boot/efi/standalone.c -o build/boot/standalone.o
"$ld" --build-id=none --no-warn-rwx-segments \
    -T boot/efi/standalone-linker.ld -o build/boot/standalone.elf build/boot/standalone.o
if "$readelf" -rW build/boot/standalone.elf | grep -Eq '\bR_X86_64_'; then
    echo "UEFI loader contains unresolved runtime relocations" >&2; exit 1
fi
"$objcopy" -O binary build/boot/standalone.elf build/boot/standalone.bin
python3 scripts/elf-to-efi.py --input build/boot/standalone.bin --output build/BOOTX64.EFI

cp build/BOOTX64.EFI iso/EFI/BOOT/BOOTX64.EFI
cp build/kernel.elf iso/kernel.elf
cp build/kernel.elf iso/EFI/BOOT/kernel.elf

version="$(sed -n 's/^#define KUROGANE_VERSION_STRING "\([^"]*\)"/\1/p' common/version.h)"
[[ -n "$version" ]] || { echo "cannot read KuroganeOS version" >&2; exit 1; }
{
    echo "version=$version"
    echo "profile=$configuration"
    echo "host=Linux $(uname -r) $(uname -m)"
    echo "architecture=x86_64"
    echo "compiler=$($cxx --version | head -n 1)"
    echo "linker=$($ld --version | head -n 1)"
} > build/build-info.txt

if $no_image; then
    echo "[linux] build complete (image skipped)"
    exit 0
fi

for tool in truncate sgdisk mkfs.fat mcopy mmd mdir sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "missing image tool: $tool" >&2
        echo "run: bash ./scripts/setup-linux.sh --install" >&2
        exit 1
    }
done

image="build/images/KuroganeOS-linux.img"
bash "$root/scripts/build-foundation-image.sh" \
    --output "$image" --efi build/BOOTX64.EFI --kernel build/kernel.elf \
    --rootfs rootfs --overlay build/userspace/rootfs

release_image="dist/KuroganeOS-$version-linux-qemu.img"
cp "$image" "$release_image"
sha256sum "$release_image" > "$release_image.sha256"

echo "[linux] KuroganeOS $version build complete"
echo "[linux] QEMU image: $release_image"

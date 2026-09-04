#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

configuration=debug
stage_only=false
clean=false
rebuild=false
no_image=false
build_iso=false

usage() {
    cat >&2 <<'USAGE'
usage: ./scripts/build-macos.sh [options]
  --configuration debug|release|test
  --clean          remove macOS build outputs and exit
  --rebuild        clean before building
  --stage-only     reuse build/kernel.elf, rebuild userspace/EFI/image
  --no-image       stop after staging EFI/kernel/userspace
  --iso            also build the installable versioned ISO in dist/
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
        --iso) build_iso=true; shift ;;
        *) usage ;;
    esac
done
case "$configuration" in debug|release|test) ;; *) usage ;; esac
if $no_image && $build_iso; then
    echo "--no-image cannot be combined with --iso" >&2
    exit 2
fi

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "build-macos.sh requires macOS. Windows remains supported by scripts/build.ps1." >&2
    exit 1
fi

# macOS/Apple Silicon hotfix:
# Never inherit generic CC/CXX/LD variables here. On an M-series Mac those
# commonly point at Apple clang/clang++, which produces Mach-O arm64 host
# objects and eventually fails with "symbol(s) not found for architecture
# arm64". KuroganeOS is an x86-64 ELF target, so only the dedicated cross
# toolchain is valid. Explicit Kurogane-specific overrides remain available for
# developers with a custom cross-toolchain path.
cc="${KUROGANE_CC:-x86_64-elf-gcc}"
cxx="${KUROGANE_CXX:-x86_64-elf-g++}"
ld="${KUROGANE_LD:-x86_64-elf-ld}"
ar="${KUROGANE_AR:-x86_64-elf-ar}"
objcopy="${KUROGANE_OBJCOPY:-x86_64-elf-objcopy}"
readelf="${KUROGANE_READELF:-x86_64-elf-readelf}"
for tool in "$cc" "$cxx" "$ld" "$ar" "$objcopy" "$readelf" python3 make; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "missing build tool: $tool" >&2
        echo "run ./scripts/setup-macos.sh --install" >&2
        exit 1
    }
done

verify_x86_64_toolchain() {
    local probe_dir probe_src probe_obj
    probe_dir="$(mktemp -d "${TMPDIR:-/tmp}/kurogane-x86_64-probe.XXXXXX")"
    probe_src="$probe_dir/probe.c"
    probe_obj="$probe_dir/probe.o"
    printf '%s\n' 'void kurogane_toolchain_probe(void) {}' > "$probe_src"
    if ! "$cc" -ffreestanding -fno-stack-protector -m64 -c "$probe_src" -o "$probe_obj" >/dev/null 2>&1; then
        rm -rf -- "$probe_dir"
        echo "[macos] x86-64 cross-compiler probe failed: $cc" >&2
        echo "[macos] run ./scripts/setup-macos.sh --install" >&2
        return 1
    fi
    if ! "$readelf" -hW "$probe_obj" 2>/dev/null | \
        grep -Eq 'Machine:[[:space:]]+Advanced Micro Devices X86-64'; then
        rm -rf -- "$probe_dir"
        echo "[macos] refusing host compiler output: KuroganeOS requires x86-64 ELF" >&2
        echo "[macos] selected compiler: $cc" >&2
        echo "[macos] do not use Apple clang/clang++; install x86_64-elf-gcc" >&2
        return 1
    fi
    rm -rf -- "$probe_dir"
    echo "[macos] cross-toolchain target: x86-64 ELF"
}
verify_x86_64_toolchain

clean_outputs() {
    rm -rf -- build/obj build/boot build/userspace build/sdk build/images
    rm -f -- build/kernel.elf build/kernel.map build/BOOTX64.EFI build/build-info.txt
    rm -rf -- build/installer-staging build/installer-iso-staging
    rm -f -- build/install.pkg
    rm -rf -- iso/EFI
    rm -f -- iso/kernel.elf
    rm -f -- dist/KuroganeOS-*-macos-qemu.img dist/KuroganeOS-*-macos-qemu.img.sha256
    rm -f -- dist/KuroganeOS-*-x86_64.iso dist/SHA256SUMS.txt kurogane.iso
    echo "[macos] build outputs cleaned (state/macos-apps preserved)"
}

if $clean; then clean_outputs; exit 0; fi
if $rebuild; then clean_outputs; fi

mkdir -p build build/userspace/rootfs build/boot iso/EFI/BOOT dist

if ! $stage_only; then
    echo "[macos] building x86-64 kernel ($configuration)"
    # Pin the Makefile target prefix as well. An exported TARGET_PREFIX from a
    # host development environment must never redirect this build to Apple
    # clang/ld on arm64.
    make CONFIG="$configuration" TARGET_PREFIX=x86_64-elf- kernel
elif [[ ! -f build/kernel.elf ]]; then
    echo "--stage-only requires build/kernel.elf" >&2
    exit 1
fi

# Build the core userspace images exactly as the Windows frontend does.
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
    "deviceprobe|userspace/system/device-probe/main.c|system/devprobe|c"
)
for spec in "${applications[@]}"; do
    IFS='|' read -r name source output kind <<<"$spec"
    object="build/userspace/$name.o"
    target="build/userspace/rootfs/$output"
    mkdir -p "$(dirname "$target")"
    common=(-ffreestanding -fno-stack-protector -m64 -mno-red-zone -Wa,--noexecstack \
        -I sdk/include -I userspace/runtime -I common)
    if [[ "$kind" == asm ]]; then
        "$cc" "${common[@]}" -c -x assembler-with-cpp "$source" -o "$object"
    else
        "$cc" "${common[@]}" -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
            -fno-builtin -fno-pic -fno-pie -mcmodel=large -c "$source" -o "$object"
    fi
    "$ld" --fatal-warnings --build-id=none -nostdlib -z noexecstack -z separate-code \
        -T userspace/linker.ld -o "$target" "$object"
    "$readelf" -hW "$target" | grep -Eq 'Type:[[:space:]]+EXEC' || {
        echo "invalid ET_EXEC userspace image: $name" >&2; exit 1; }
    "$readelf" -hW "$target" | grep -Eq 'Machine:[[:space:]]+Advanced Micro Devices X86-64' || {
        echo "invalid x86-64 userspace image: $name" >&2; exit 1; }
    if "$readelf" -lW "$target" | grep -Eq '^[[:space:]]*(LOAD|GNU_STACK).*RWE'; then
        echo "userspace image has W+X segment/stack: $name" >&2; exit 1
    fi
    echo "[userspace] /$output"
done

# SDK build adds the external sample and Ring-3 GUI applications to the same
# userspace overlay. Pass only Kurogane-specific tool variables so ambient
# CC/CXX=clang/clang++ cannot leak into the x86-64 target build. Invoke nested
# shell pipelines through bash so a checkout that lost executable mode bits
# cannot break an otherwise valid macOS build.
KUROGANE_CC="$cc" \
KUROGANE_CXX="$cxx" \
KUROGANE_AR="$ar" \
KUROGANE_READELF="$readelf" \
    bash "$root/scripts/build-sdk.sh"

# The live IMG and the installer package must boot with the same Web PKI trust
# set. Export only Apple's immutable SystemRootCertificates keychain into the
# userspace overlay. build-foundation-image-macos.sh overlays it onto the live
# root filesystem, while build-installer-macos.sh packages that exact overlay
# into install.pkg when --iso is requested.
bash "$root/scripts/export-macos-trust-store.sh" \
    "$root/build/userspace/rootfs/etc/ssl/certs.pem"

# User-built applications installed with build-app-macos.sh live in state/ so
# clean/rebuild does not silently destroy developer work.
if [[ -d state/macos-apps ]]; then
    mkdir -p build/userspace/rootfs/apps
    while IFS= read -r -d '' app; do
        name="$(basename "$app")"
        cp "$app" "build/userspace/rootfs/apps/$name"
        echo "[userspace] /apps/$name (macOS development app)"
    done < <(find state/macos-apps -maxdepth 1 -type f -print0)
fi

# Standalone UEFI loader. Keep flags/layout identical to scripts/build.ps1.
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
    echo "UEFI loader contains unresolved runtime relocations" >&2
    exit 1
fi
"$objcopy" -O binary build/boot/standalone.elf build/boot/standalone.bin
python3 scripts/elf-to-efi.py --input build/boot/standalone.bin --output build/BOOTX64.EFI

# Lightweight PE sanity check independent of host tools.
python3 - build/BOOTX64.EFI <<'PY'
import struct, sys
p=sys.argv[1]; b=open(p,'rb').read()
if len(b)<256 or b[:2]!=b'MZ': raise SystemExit('invalid EFI MZ header')
o=struct.unpack_from('<I',b,0x3c)[0]
if b[o:o+4]!=b'PE\0\0': raise SystemExit('invalid EFI PE signature')
if struct.unpack_from('<H',b,o+4)[0]!=0x8664: raise SystemExit('EFI is not AMD64')
opt=o+24
if struct.unpack_from('<H',b,opt)[0]!=0x20b: raise SystemExit('EFI is not PE32+')
if struct.unpack_from('<H',b,opt+68)[0]!=10: raise SystemExit('EFI subsystem is not application')
PY

cp build/BOOTX64.EFI iso/EFI/BOOT/BOOTX64.EFI
cp build/kernel.elf iso/kernel.elf
cp build/kernel.elf iso/EFI/BOOT/kernel.elf

echo "[stage] iso/EFI/BOOT/BOOTX64.EFI"
echo "[stage] iso/kernel.elf"

version="$(bash "$root/scripts/read-version.sh")"
{
    echo "version=$version"
    echo "profile=$configuration"
    echo "host=macOS $(sw_vers -productVersion) $(uname -m)"
    echo "architecture=x86_64"
    echo "compiler=$($cxx --version | head -n 1)"
    echo "linker=$($ld --version | head -n 1)"
} > build/build-info.txt

if $no_image; then
    echo "[macos] build complete (image skipped)"
    exit 0
fi

for tool in sgdisk mkfs.fat fsck.fat mcopy mmd mdir; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "missing image tool: $tool; run ./scripts/setup-macos.sh --install" >&2; exit 1; }
done

image="build/images/KuroganeOS-macos.img"
bash "$root/scripts/build-foundation-image-macos.sh" \
    --output "$image" --efi build/BOOTX64.EFI --kernel build/kernel.elf \
    --rootfs rootfs --overlay build/userspace/rootfs

release_image="dist/KuroganeOS-$version-macos-qemu.img"
cp "$image" "$release_image"
shasum -a 256 "$release_image" > "$release_image.sha256"

echo "[macos] KuroganeOS $version build complete"
echo "[macos] kernel: build/kernel.elf"
echo "[macos] EFI: build/BOOTX64.EFI"
echo "[macos] SDK: build/sdk/sysroot"
echo "[macos] QEMU image: $release_image"

if $build_iso; then
    echo "[macos] building installable ISO from the same live userspace/trust overlay"
    bash "$root/scripts/build-installer-macos.sh" \
        --configuration "$configuration" --no-build
fi

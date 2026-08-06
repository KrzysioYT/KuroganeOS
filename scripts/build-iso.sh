#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIGURATION="${1:-release}"
BUILD_DIR="$ROOT_DIR/build"
STAGING_DIR="$ROOT_DIR/iso"
EFI_DIR="$STAGING_DIR/EFI/BOOT"
ISO_STAGING_DIR="$BUILD_DIR/iso-staging"
IMG_PATH="$ROOT_DIR/kurogane.img"
ISO_PATH="$ROOT_DIR/kurogane.iso"
BUILD_SCRIPT="$ROOT_DIR/scripts/build.ps1"

die() {
  printf 'ERROR: %s\n' "$*" >&2
  exit 1
}

case "$CONFIGURATION" in
  debug|release) ;;
  *) die "Usage: $0 [debug|release]" ;;
esac

require_command() {
  command -v "$1" >/dev/null 2>&1 ||
    die "Missing required command '$1'. Install xorriso before building the ISO."
}

if command -v powershell.exe >/dev/null 2>&1; then
  POWERSHELL_BIN="powershell.exe"
  POWERSHELL_IS_WINDOWS=1
elif command -v pwsh.exe >/dev/null 2>&1; then
  POWERSHELL_BIN="pwsh.exe"
  POWERSHELL_IS_WINDOWS=1
elif command -v pwsh >/dev/null 2>&1; then
  POWERSHELL_BIN="pwsh"
  POWERSHELL_IS_WINDOWS=0
else
  die "Neither powershell.exe nor pwsh is available; cannot run scripts/build.ps1."
fi

to_windows_path() {
  local path="$1"
  if command -v cygpath >/dev/null 2>&1; then
    cygpath -w "$path"
  elif command -v wslpath >/dev/null 2>&1; then
    wslpath -w "$path"
  else
    printf '%s\n' "$path"
  fi
}

run_kernel_build() {
  if (( POWERSHELL_IS_WINDOWS )); then
    "$POWERSHELL_BIN" -NoLogo -NoProfile -ExecutionPolicy Bypass \
      -File "$(to_windows_path "$BUILD_SCRIPT")" \
      -Configuration "$CONFIGURATION"
  else
    "$POWERSHELL_BIN" -NoLogo -NoProfile -File "$BUILD_SCRIPT" \
      -Configuration "$CONFIGURATION"
  fi
}

require_command xorriso

# PowerShell is the single source of truth for compiling and staging the
# kernel and the self-contained UEFI bootloader.
run_kernel_build

[[ -f "$BUILD_DIR/kernel.elf" ]] ||
  die "Kernel build did not produce $BUILD_DIR/kernel.elf."
[[ -f "$EFI_DIR/BOOTX64.EFI" ]] ||
  die "Validated EFI application is missing from $EFI_DIR/BOOTX64.EFI."
[[ -f "$IMG_PATH" ]] ||
  die "Validated FAT32 image is missing from $IMG_PATH."

# Embed the deterministic FAT32 image produced and validated by build.ps1 as
# the sole UEFI El Torito boot image.
case "$ISO_STAGING_DIR" in
  "$ROOT_DIR"/build/iso-staging) ;;
  *) die "Refusing to clear unexpected ISO staging path: $ISO_STAGING_DIR" ;;
esac
rm -rf -- "$ISO_STAGING_DIR"
mkdir -p "$ISO_STAGING_DIR"
cp "$IMG_PATH" "$ISO_STAGING_DIR/efiboot.img"
# Also expose the fallback UEFI path in the ISO filesystem. The embedded ESP
# remains the actual El Torito boot image, while this copy makes the medium
# understandable to firmware/tools that inspect the ISO tree directly and to
# Windows utilities that look specifically for /EFI/BOOT/BOOTX64.EFI.
cp -R "$STAGING_DIR/EFI" "$ISO_STAGING_DIR/EFI"
cp "$STAGING_DIR/kernel.elf" "$ISO_STAGING_DIR/kernel.elf"

rm -f -- "$ISO_PATH"
xorriso -as mkisofs \
  -R -J -V KUROGANEOS \
  -o "$ISO_PATH" \
  -e efiboot.img \
  -no-emul-boot \
  -isohybrid-gpt-basdat \
  "$ISO_STAGING_DIR"

printf 'Built %s\n' "$IMG_PATH"
printf 'Built %s\n' "$ISO_PATH"

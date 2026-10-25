#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat >&2 <<'EOF'
usage: ./scripts/verify-virtualbox-iso.sh ISO [--passes N] [--virtualbox-smoke]

Performs deterministic VirtualBox-targeted x86-64 UEFI/El Torito/FAT/GPT/PE
checks repeatedly. With --virtualbox-smoke it also boots the ISO in a temporary
VirtualBox EFI64 VM and requires the KuroganeOS kernel serial marker.
EOF
    exit 2
}

iso=""
passes=20
virtualbox_smoke=false
while (($#)); do
    case "$1" in
        --passes) [[ $# -ge 2 ]] || usage; passes="$2"; shift 2 ;;
        --virtualbox-smoke) virtualbox_smoke=true; shift ;;
        -h|--help) usage ;;
        -*) usage ;;
        *) [[ -z "$iso" ]] || usage; iso="$1"; shift ;;
    esac
done
[[ -n "$iso" ]] || usage
[[ "$passes" =~ ^[0-9]+$ ]] && ((passes >= 1 && passes <= 100)) || {
    echo "--passes must be between 1 and 100" >&2; exit 2; }
iso="$(cd "$(dirname "$iso")" && pwd)/$(basename "$iso")"
[[ -f "$iso" ]] || { echo "ISO not found: $iso" >&2; exit 1; }
[[ -s "$iso" ]] || { echo "ISO is empty: $iso" >&2; exit 1; }

for tool in xorriso mdir mcopy python3; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "required verifier tool missing: $tool" >&2; exit 1; }
done
if command -v fsck.fat >/dev/null 2>&1; then
    fsck_tool=fsck.fat
elif command -v dosfsck >/dev/null 2>&1; then
    fsck_tool=dosfsck
else
    echo "fsck.fat/dosfsck is required" >&2
    exit 1
fi
if command -v sha256sum >/dev/null 2>&1; then
    hash_iso() { sha256sum "$1" | awk '{print $1}'; }
elif command -v shasum >/dev/null 2>&1; then
    hash_iso() { shasum -a 256 "$1" | awk '{print $1}'; }
else
    echo "sha256sum or shasum is required" >&2
    exit 1
fi

readonly expected_hash="$(hash_iso "$iso")"
readonly tmp="$(mktemp -d "${TMPDIR:-/tmp}/kurogane-vbox-iso.XXXXXX")"
cleanup() { rm -rf -- "$tmp"; }
trap cleanup EXIT INT TERM

check_pe_amd64_efi() {
    python3 - "$1" <<'PY'
import struct, sys
path=sys.argv[1]
data=open(path,'rb').read()
if len(data) < 256 or data[:2] != b'MZ':
    raise SystemExit('BOOTX64.EFI: missing MZ header')
pe=struct.unpack_from('<I', data, 0x3c)[0]
if pe + 96 > len(data) or data[pe:pe+4] != b'PE\0\0':
    raise SystemExit('BOOTX64.EFI: invalid PE signature')
if struct.unpack_from('<H', data, pe+4)[0] != 0x8664:
    raise SystemExit('BOOTX64.EFI: PE machine is not AMD64')
opt=pe+24
if struct.unpack_from('<H', data, opt)[0] != 0x20b:
    raise SystemExit('BOOTX64.EFI: not PE32+')
if struct.unpack_from('<H', data, opt+68)[0] != 10:
    raise SystemExit('BOOTX64.EFI: subsystem is not EFI application')
PY
}

for ((pass=1; pass<=passes; ++pass)); do
    pass_dir="$tmp/pass-$pass"
    mkdir -p "$pass_dir"

    current_hash="$(hash_iso "$iso")"
    [[ "$current_hash" == "$expected_hash" ]] || {
        echo "ISO hash changed during verification pass $pass" >&2; exit 1; }

    report="$pass_dir/el-torito.txt"
    xorriso -indev "$iso" -report_el_torito plain >"$report" 2>&1
    grep -Eqi 'El Torito|boot image' "$report" || {
        echo "pass $pass: no El Torito boot record" >&2; cat "$report" >&2; exit 1; }
    grep -Eqi 'EFI|UEFI|0xEF|0xef' "$report" || {
        echo "pass $pass: El Torito record is not EFI platform 0xEF" >&2
        cat "$report" >&2; exit 1; }

    # This is the key VirtualBox-media invariant: the EFI El Torito catalog
    # entry must target the same appended ESP which is represented in GPT.
    repro="$pass_dir/el-torito-mkisofs.txt"
    xorriso -indev "$iso" -report_el_torito as_mkisofs >"$repro" 2>&1
    grep -Fq 'appended_partition_2' "$repro" || {
        echo "pass $pass: EFI El Torito entry does not reference appended GPT ESP #2" >&2
        cat "$repro" >&2; exit 1; }

    system_report="$pass_dir/system-area.txt"
    xorriso -indev "$iso" -report_system_area plain >"$system_report" 2>&1
    grep -Eqi 'GPT' "$system_report" || {
        echo "pass $pass: ISO has no GPT system area" >&2
        cat "$system_report" >&2; exit 1; }
    grep -Eqi 'EFI|c12a7328|C12A7328|0xef' "$system_report" || {
        echo "pass $pass: GPT has no EFI System Partition" >&2
        cat "$system_report" >&2; exit 1; }

    for path in /efiboot.img /EFI/BOOT/BOOTX64.EFI /kernel.elf /install.pkg; do
        target="$pass_dir/$(basename "$path")"
        xorriso -osirrox on -indev "$iso" -extract "$path" "$target" \
            >/dev/null 2>&1 || {
                echo "pass $pass: ISO is missing $path" >&2; exit 1; }
        [[ -s "$target" ]] || {
            echo "pass $pass: extracted $path is empty" >&2; exit 1; }
    done

    esp="$pass_dir/efiboot.img"
    esp_bytes="$(wc -c < "$esp" | tr -d ' ')"
    [[ "$esp_bytes" =~ ^[0-9]+$ ]] || {
        echo "pass $pass: cannot determine EFI image size" >&2; exit 1; }
    ((esp_bytes % 512 == 0)) || {
        echo "pass $pass: EFI image is not 512-byte sector aligned" >&2; exit 1; }
    esp_sectors=$((esp_bytes / 512))
    ((esp_sectors > 0 && esp_sectors < 65535)) || {
        echo "pass $pass: EFI boot image has $esp_sectors sectors (must be <65535)" >&2
        exit 1
    }

    "$fsck_tool" -n "$esp" >/dev/null
    mdir -i "$esp" ::/EFI >/dev/null
    mdir -i "$esp" ::/EFI/BOOT >/dev/null
    mdir -i "$esp" ::/EFI/BOOT/BOOTX64.EFI >/dev/null
    mdir -i "$esp" ::/EFI/BOOT/kernel.elf >/dev/null
    mdir -i "$esp" ::/kernel.elf >/dev/null
    mdir -i "$esp" ::/install.pkg >/dev/null

    inner_loader="$pass_dir/BOOTX64-inner.EFI"
    mcopy -o -i "$esp" ::/EFI/BOOT/BOOTX64.EFI "$inner_loader" >/dev/null
    [[ -s "$inner_loader" ]] || {
        echo "pass $pass: FAT EFI loader is empty" >&2; exit 1; }
    check_pe_amd64_efi "$pass_dir/BOOTX64.EFI"
    check_pe_amd64_efi "$inner_loader"

    outer_loader_hash="$(hash_iso "$pass_dir/BOOTX64.EFI")"
    inner_loader_hash="$(hash_iso "$inner_loader")"
    [[ "$outer_loader_hash" == "$inner_loader_hash" ]] || {
        echo "pass $pass: ISO loader and ESP EFI loader differ" >&2; exit 1; }

    echo "[virtualbox-iso] pass $pass/$passes: PASS (GPT ESP #2, EFI sectors=$esp_sectors)"
done

if $virtualbox_smoke; then
    command -v VBoxManage >/dev/null 2>&1 || {
        echo "--virtualbox-smoke requested but VBoxManage is unavailable" >&2
        exit 1
    }
    machine="KuroganeOS-ISO-Smoke-$$"
    serial="$tmp/virtualbox-serial.log"
    vdi="$tmp/KuroganeOS-smoke.vdi"

    cleanup_vm() {
        VBoxManage controlvm "$machine" poweroff >/dev/null 2>&1 || true
        VBoxManage unregistervm "$machine" --delete >/dev/null 2>&1 || true
    }
    trap 'cleanup_vm; cleanup' EXIT INT TERM

    VBoxManage createvm --name "$machine" --ostype Other_64 --register >/dev/null
    VBoxManage modifyvm "$machine" \
        --memory 1024 --cpus 1 --firmware efi64 --ioapic on \
        --boot1 dvd --boot2 disk --boot3 none --boot4 none \
        --keyboard ps2 --mouse ps2 >/dev/null

    if ! VBoxManage modifyvm "$machine" \
        --nic1 nat --nic-type1 82540EM --cable-connected1 on >/dev/null 2>&1; then
        VBoxManage modifyvm "$machine" \
            --nic1 nat --nictype1 82540EM --cableconnected1 on >/dev/null
    fi
    VBoxManage modifyvm "$machine" --uart1 0x3F8 4 >/dev/null
    VBoxManage modifyvm "$machine" --uartmode1 file "$serial" >/dev/null

    VBoxManage createmedium disk --filename "$vdi" --size 1024 --format VDI >/dev/null
    VBoxManage storagectl "$machine" --name "SATA" --add sata --controller IntelAHCI >/dev/null
    VBoxManage storageattach "$machine" --storagectl "SATA" --port 0 --device 0 \
        --type hdd --medium "$vdi" >/dev/null
    VBoxManage storagectl "$machine" --name "IDE" --add ide --controller PIIX4 >/dev/null
    VBoxManage storageattach "$machine" --storagectl "IDE" --port 0 --device 0 \
        --type dvddrive --medium "$iso" >/dev/null

    VBoxManage startvm "$machine" --type headless >/dev/null
    deadline=$((SECONDS + 90))
    booted=false
    while ((SECONDS < deadline)); do
        if [[ -f "$serial" ]] && grep -Eq \
            'KuroganeOS kernel entry|\[TEST\] paging: PASS|KUROGANE OS' "$serial"; then
            booted=true
            break
        fi
        sleep 1
    done
    if ! $booted; then
        echo "VirtualBox EFI64 smoke boot did not reach the KuroganeOS kernel" >&2
        [[ -f "$serial" ]] && tail -n 100 "$serial" >&2 || true
        exit 1
    fi
    echo "[virtualbox-iso] real VirtualBox EFI64 optical boot: PASS"
    cleanup_vm
fi

echo "[virtualbox-iso] VIRTUALBOX ISO VERIFIED ($passes passes)"
echo "[virtualbox-iso] SHA-256: $expected_hash"

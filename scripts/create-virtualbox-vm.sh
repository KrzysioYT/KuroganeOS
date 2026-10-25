#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat >&2 <<'EOF'
usage: ./scripts/create-virtualbox-vm.sh --iso FILE [--name NAME] [--disk FILE] [--disk-size MB] [--nic e1000|virtio|pcnet]

Creates a reference KuroganeOS x86-64 VirtualBox VM:
  EFI64, 1 GiB RAM, single-port SATA/AHCI disk, ISO DVD, NAT, AC97, PS/2 input.
The default NIC remains E1000 for compatibility; VirtIO and PCnet can be selected.
EOF
    exit 2
}

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="$(bash "$root/scripts/read-version.sh" 2>/dev/null || true)"
[[ -n "$version" ]] || version="DEV"

iso=""
name="KuroganeOS $version"
disk=""
disk_size=2048
nic="e1000"
while (($#)); do
    case "$1" in
        --iso) iso="${2:-}"; shift 2 ;;
        --name) name="${2:-}"; shift 2 ;;
        --disk) disk="${2:-}"; shift 2 ;;
        --disk-size) disk_size="${2:-}"; shift 2 ;;
        --nic) nic="${2:-}"; shift 2 ;;
        -h|--help) usage ;;
        *) usage ;;
    esac
done
[[ -n "$iso" ]] || usage
case "$nic" in
    e1000) nic_type="82540EM" ;;
    virtio) nic_type="virtio" ;;
    pcnet) nic_type="Am79C973" ;;
    *) echo "unsupported NIC profile: $nic" >&2; usage ;;
esac
command -v VBoxManage >/dev/null 2>&1 || {
    echo "VBoxManage is not installed or not in PATH" >&2; exit 1; }
iso="$(cd "$(dirname "$iso")" && pwd)/$(basename "$iso")"
[[ -f "$iso" ]] || { echo "ISO not found: $iso" >&2; exit 1; }
[[ "$iso" == *.iso ]] || {
    echo "VirtualBox optical boot requires the KuroganeOS .iso, not an .img: $iso" >&2; exit 1; }
[[ "$disk_size" =~ ^[0-9]+$ ]] && ((disk_size >= 1024)) || {
    echo "--disk-size must be at least 1024 MB" >&2; exit 2; }

if [[ -z "$disk" ]]; then
    base="${HOME}/VirtualBox VMs/$name"
    mkdir -p "$base"
    disk="$base/KuroganeOS.vdi"
else
    mkdir -p "$(dirname "$disk")"
    disk="$(cd "$(dirname "$disk")" && pwd)/$(basename "$disk")"
fi

if VBoxManage showvminfo "$name" >/dev/null 2>&1; then
    echo "VirtualBox VM already exists: $name" >&2
    exit 1
fi

VBoxManage createvm --name "$name" --register >/dev/null
cleanup_on_error=true
cleanup() {
    if $cleanup_on_error; then
        VBoxManage unregistervm "$name" --delete >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT INT TERM

VBoxManage modifyvm "$name" \
    --memory 1024 --cpus 2 --firmware efi64 --ioapic on \
    --boot1 dvd --boot2 disk --boot3 none --boot4 none \
    --graphicscontroller vboxsvga --vram 64 \
    --keyboard ps2 --mouse ps2 >/dev/null

if ! VBoxManage modifyvm "$name" \
    --nic1 nat --nic-type1 "$nic_type" --cable-connected1 on >/dev/null 2>&1; then
    VBoxManage modifyvm "$name" \
        --nic1 nat --nictype1 "$nic_type" --cableconnected1 on >/dev/null
fi
if ! VBoxManage modifyvm "$name" \
    --audio-enabled on --audio-controller ac97 --audio-out on >/dev/null 2>&1; then
    VBoxManage modifyvm "$name" --audio on --audiocontroller ac97 >/dev/null
fi

if [[ ! -f "$disk" ]]; then
    VBoxManage createmedium disk --filename "$disk" \
        --size "$disk_size" --format VDI >/dev/null
fi
# A single-disk KuroganeOS VM only needs one implemented AHCI port. Keeping the
# VirtualBox controller bounded avoids spending boot time probing empty ports.
VBoxManage storagectl "$name" --name "SATA" --add sata \
    --controller IntelAHCI --portcount 1 >/dev/null
VBoxManage storageattach "$name" --storagectl "SATA" --port 0 --device 0 \
    --type hdd --medium "$disk" >/dev/null
VBoxManage storagectl "$name" --name "IDE" --add ide --controller PIIX4 >/dev/null
VBoxManage storageattach "$name" --storagectl "IDE" --port 0 --device 0 \
    --type dvddrive --medium "$iso" >/dev/null

# KuroganeOS uses GOP; 1280x800 is a comfortable development mode.
VBoxManage setextradata "$name" VBoxInternal2/EfiGraphicsResolution 1280x800 >/dev/null

cleanup_on_error=false
trap - EXIT INT TERM

echo "Created VirtualBox VM: $name"
echo "ISO: $iso"
echo "Disk: $disk"
echo "NIC: $nic ($nic_type), NAT"
echo "Storage: IntelAHCI/SATA port 0 -> VDI; IDE/PIIX4 -> ISO DVD"
echo "Firmware: EFI64; Boot order: DVD -> Disk"
echo "Start with: VBoxManage startvm \"$name\""

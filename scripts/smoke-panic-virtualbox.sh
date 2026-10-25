#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 IMAGE [--nested] [--timeout SECONDS]" >&2
    exit 2
}

image=""
nested=false
timeout_seconds=180
while (($#)); do
    case "$1" in
        --nested) nested=true; shift ;;
        --timeout) [[ $# -ge 2 ]] || usage; timeout_seconds="$2"; shift 2 ;;
        -h|--help) usage ;;
        -*) usage ;;
        *) [[ -z "$image" ]] || usage; image="$1"; shift ;;
    esac
done
[[ -n "$image" ]] || usage
[[ "$timeout_seconds" =~ ^[0-9]+$ ]] && ((timeout_seconds >= 10 && timeout_seconds <= 300)) || usage

if ! command -v VBoxManage >/dev/null 2>&1; then
    echo "VirtualBox panic qualification PENDING/EXTERNAL: VBoxManage is unavailable on this host" >&2
    exit 2
fi
command -v python3 >/dev/null 2>&1 || {
    echo "python3 is required" >&2
    exit 1
}
image="$(cd "$(dirname "$image")" && pwd)/$(basename "$image")"
[[ -f "$image" && -s "$image" ]] || {
    echo "panic smoke image missing or empty: $image" >&2
    exit 1
}

tmp="$(mktemp -d "${TMPDIR:-/tmp}/kurogane-panic-vbox.XXXXXX")"
serial="$tmp/serial.log"
disk="$tmp/kurogane-panic.vdi"
vm="KuroganeOS-panic-$RANDOM-$$"
registered=false
started=false
cleanup() {
    if $started; then
        VBoxManage controlvm "$vm" poweroff >/dev/null 2>&1 || true
    fi
    if $registered; then
        VBoxManage unregistervm "$vm" --delete >/dev/null 2>&1 || true
    fi
    rm -rf -- "$tmp"
}
trap cleanup EXIT INT TERM

VBoxManage convertfromraw "$image" "$disk" --format VDI >/dev/null
VBoxManage createvm --name "$vm" --ostype Other_64 --register >/dev/null
registered=true
VBoxManage modifyvm "$vm" \
    --firmware efi \
    --memory 1024 \
    --cpus 1 \
    --ioapic on \
    --nic1 none \
    --uart1 0x3F8 4 \
    --uartmode1 file "$serial" >/dev/null
VBoxManage storagectl "$vm" --name SATA --add sata --controller IntelAhci >/dev/null
VBoxManage storageattach "$vm" \
    --storagectl SATA --port 0 --device 0 --type hdd --medium "$disk" >/dev/null
VBoxManage startvm "$vm" --type headless >/dev/null
started=true

wanted="=== KUROGANE_FATAL_END ==="
$nested && wanted="=== KUROGANE_FATAL_NESTED ==="
deadline=$((SECONDS + timeout_seconds))
while ((SECONDS < deadline)); do
    if [[ -f "$serial" ]] && grep -Fq "$wanted" "$serial"; then
        checker=(python3 scripts/check-panic-serial.py "$serial")
        $nested && checker+=(--nested)
        "${checker[@]}"
        echo "[panic-virtualbox] deliberate panic qualification: PASS"
        exit 0
    fi
    if [[ -f "$serial" ]] && grep -Fq '[TEST] ALL_REQUIRED_TESTS_PASSED: FAIL' "$serial"; then
        echo "ordinary KuroganeOS boot/runtime failure occurred before deliberate panic" >&2
        tail -n 220 "$serial" >&2 || true
        exit 1
    fi
    state="$(VBoxManage showvminfo "$vm" --machinereadable 2>/dev/null | sed -n 's/^VMState="\([^"]*\)"/\1/p')"
    if [[ "$state" != "running" ]]; then
        echo "VirtualBox VM stopped before deliberate panic marker (state=$state)" >&2
        [[ -f "$serial" ]] && cat "$serial" >&2 || true
        exit 1
    fi
    sleep 1
done

echo "VirtualBox did not reach deliberate panic marker within $timeout_seconds seconds" >&2
[[ -f "$serial" ]] && tail -n 260 "$serial" >&2 || true
exit 1

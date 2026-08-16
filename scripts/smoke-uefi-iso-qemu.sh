#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: ./scripts/smoke-uefi-iso-qemu.sh ISO [--timeout SECONDS]" >&2
    exit 2
}

iso=""
timeout_seconds=60
while (($#)); do
    case "$1" in
        --timeout) [[ $# -ge 2 ]] || usage; timeout_seconds="$2"; shift 2 ;;
        -h|--help) usage ;;
        -*) usage ;;
        *) [[ -z "$iso" ]] || usage; iso="$1"; shift ;;
    esac
done
[[ -n "$iso" ]] || usage
[[ "$timeout_seconds" =~ ^[0-9]+$ ]] && ((timeout_seconds >= 10 && timeout_seconds <= 180)) || {
    echo "invalid timeout" >&2; exit 2; }
command -v qemu-system-x86_64 >/dev/null 2>&1 || {
    echo "qemu-system-x86_64 is required" >&2; exit 1; }
iso="$(cd "$(dirname "$iso")" && pwd)/$(basename "$iso")"
[[ -f "$iso" && -s "$iso" ]] || { echo "ISO missing or empty: $iso" >&2; exit 1; }

firmware="${OVMF_CODE:-}"
if [[ -z "$firmware" ]]; then
    candidates=(
        /usr/share/OVMF/OVMF_CODE.fd
        /usr/share/OVMF/OVMF_CODE_4M.fd
        /usr/share/edk2/x64/OVMF_CODE.fd
        /usr/share/edk2/ovmf/OVMF_CODE.fd
    )
    if command -v brew >/dev/null 2>&1; then
        candidates+=("$(brew --prefix qemu 2>/dev/null || true)/share/qemu/edk2-x86_64-code.fd")
    fi
    for candidate in "${candidates[@]}"; do
        if [[ -n "$candidate" && -f "$candidate" ]]; then
            firmware="$candidate"
            break
        fi
    done
fi
[[ -n "$firmware" && -f "$firmware" ]] || {
    echo "OVMF/edk2 x86-64 firmware not found; set OVMF_CODE=/path/to/OVMF_CODE.fd" >&2
    exit 1
}

tmp="$(mktemp -d "${TMPDIR:-/tmp}/kurogane-uefi-iso.XXXXXX")"
serial="$tmp/serial.log"
qemu_log="$tmp/qemu.log"
pid=""
cleanup() {
    if [[ -n "$pid" ]] && kill -0 "$pid" >/dev/null 2>&1; then
        kill "$pid" >/dev/null 2>&1 || true
        wait "$pid" >/dev/null 2>&1 || true
    fi
    rm -rf -- "$tmp"
}
trap cleanup EXIT INT TERM

qemu-system-x86_64 \
    -machine q35,accel=tcg \
    -cpu max \
    -m 1024 \
    -bios "$firmware" \
    -boot order=d,menu=off \
    -cdrom "$iso" \
    -serial "file:$serial" \
    -display none \
    -net none \
    -no-reboot \
    -no-shutdown \
    >"$qemu_log" 2>&1 &
pid=$!

deadline=$((SECONDS + timeout_seconds))
while ((SECONDS < deadline)); do
    if [[ -f "$serial" ]] && grep -Eq \
        'KuroganeOS kernel entry|\[TEST\] paging: PASS|KUROGANE OS' "$serial"; then
        echo "[uefi-iso-qemu] optical UEFI boot: PASS"
        echo "[uefi-iso-qemu] firmware: $firmware"
        exit 0
    fi
    if ! kill -0 "$pid" >/dev/null 2>&1; then
        echo "QEMU exited before KuroganeOS kernel marker" >&2
        [[ -f "$qemu_log" ]] && cat "$qemu_log" >&2 || true
        [[ -f "$serial" ]] && cat "$serial" >&2 || true
        exit 1
    fi
    sleep 1
done

echo "OVMF/QEMU did not boot the KuroganeOS ISO within $timeout_seconds seconds" >&2
[[ -f "$qemu_log" ]] && tail -n 100 "$qemu_log" >&2 || true
[[ -f "$serial" ]] && tail -n 100 "$serial" >&2 || true
exit 1

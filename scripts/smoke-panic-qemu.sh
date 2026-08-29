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

command -v qemu-system-x86_64 >/dev/null 2>&1 || {
    echo "qemu-system-x86_64 is required" >&2
    exit 1
}
command -v python3 >/dev/null 2>&1 || {
    echo "python3 is required" >&2
    exit 1
}
image="$(cd "$(dirname "$image")" && pwd)/$(basename "$image")"
[[ -f "$image" && -s "$image" ]] || {
    echo "panic smoke image missing or empty: $image" >&2
    exit 1
}

firmware_code="${OVMF_CODE:-}"
firmware_vars_template="${OVMF_VARS:-}"
if [[ -z "$firmware_code" ]]; then
    for candidate in \
        /usr/share/OVMF/OVMF_CODE_4M.fd \
        /usr/share/OVMF/OVMF_CODE.fd \
        /usr/share/edk2/x64/OVMF_CODE.fd \
        /usr/share/edk2/ovmf/OVMF_CODE.fd; do
        if [[ -f "$candidate" ]]; then
            firmware_code="$candidate"
            break
        fi
    done
fi
if [[ -z "$firmware_vars_template" && -n "$firmware_code" ]]; then
    case "$firmware_code" in
        */OVMF_CODE_4M.fd)
            candidate="${firmware_code%/OVMF_CODE_4M.fd}/OVMF_VARS_4M.fd"
            [[ -f "$candidate" ]] && firmware_vars_template="$candidate"
            ;;
        */OVMF_CODE.fd)
            candidate="${firmware_code%/OVMF_CODE.fd}/OVMF_VARS.fd"
            [[ -f "$candidate" ]] && firmware_vars_template="$candidate"
            ;;
    esac
fi
[[ -n "$firmware_code" && -f "$firmware_code" ]] || {
    echo "OVMF x86-64 CODE firmware not found" >&2
    exit 1
}
[[ -n "$firmware_vars_template" && -f "$firmware_vars_template" ]] || {
    echo "matching OVMF VARS firmware not found" >&2
    exit 1
}

tmp="$(mktemp -d "${TMPDIR:-/tmp}/kurogane-panic-qemu.XXXXXX")"
serial="$tmp/serial.log"
qemu_log="$tmp/qemu.log"
vars="$tmp/OVMF_VARS.fd"
pid=""
cleanup() {
    if [[ -n "$pid" ]] && kill -0 "$pid" >/dev/null 2>&1; then
        kill "$pid" >/dev/null 2>&1 || true
        wait "$pid" >/dev/null 2>&1 || true
    fi
    rm -rf -- "$tmp"
}
trap cleanup EXIT INT TERM
cp "$firmware_vars_template" "$vars"

qemu-system-x86_64 \
    -machine q35,accel=tcg \
    -cpu max \
    -m 1024 \
    -drive if=pflash,format=raw,unit=0,readonly=on,file="$firmware_code" \
    -drive if=pflash,format=raw,unit=1,file="$vars" \
    -drive "if=none,id=kurogane_system,format=raw,file=$image,snapshot=on,cache=writeback" \
    -device ide-hd,drive=kurogane_system,bus=ide.0,bootindex=1 \
    -serial "file:$serial" \
    -display none \
    -net none \
    -no-reboot \
    -no-shutdown \
    >"$qemu_log" 2>&1 &
pid=$!

wanted="=== KUROGANE_FATAL_END ==="
$nested && wanted="=== KUROGANE_FATAL_NESTED ==="
deadline=$((SECONDS + timeout_seconds))
while ((SECONDS < deadline)); do
    if [[ -f "$serial" ]] && grep -Fq "$wanted" "$serial"; then
        checker=(python3 scripts/check-panic-serial.py "$serial")
        $nested && checker+=(--nested)
        if "${checker[@]}"; then
            echo "[panic-qemu] deliberate panic qualification: PASS"
            exit 0
        fi
        cat "$serial" >&2 || true
        exit 1
    fi
    if [[ -f "$serial" ]] && grep -Fq '[TEST] ALL_REQUIRED_TESTS_PASSED: FAIL' "$serial"; then
        echo "ordinary KuroganeOS boot/runtime failure occurred before deliberate panic" >&2
        tail -n 220 "$serial" >&2 || true
        exit 1
    fi
    if ! kill -0 "$pid" >/dev/null 2>&1; then
        echo "QEMU exited before deliberate panic marker" >&2
        cat "$qemu_log" >&2 || true
        cat "$serial" >&2 || true
        exit 1
    fi
    sleep 1
done

echo "QEMU did not reach deliberate panic marker within $timeout_seconds seconds" >&2
cat "$qemu_log" >&2 || true
tail -n 260 "$serial" >&2 || true
exit 1

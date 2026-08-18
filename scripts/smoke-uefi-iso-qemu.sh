#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: ./scripts/smoke-uefi-iso-qemu.sh MEDIA [--disk] [--timeout SECONDS] [--nic none|e1000|pcnet|virtio] [--require-network]" >&2
    exit 2
}

media=""
media_kind="iso"
timeout_seconds=60
nic_model="none"
require_network=false
while (($#)); do
    case "$1" in
        --disk) media_kind="disk"; shift ;;
        --timeout) [[ $# -ge 2 ]] || usage; timeout_seconds="$2"; shift 2 ;;
        --nic) [[ $# -ge 2 ]] || usage; nic_model="$2"; shift 2 ;;
        --require-network) require_network=true; shift ;;
        -h|--help) usage ;;
        -*) usage ;;
        *) [[ -z "$media" ]] || usage; media="$1"; shift ;;
    esac
done
[[ -n "$media" ]] || usage
[[ "$timeout_seconds" =~ ^[0-9]+$ ]] && ((timeout_seconds >= 10 && timeout_seconds <= 180)) || {
    echo "invalid timeout" >&2; exit 2; }
case "$nic_model" in
    none|e1000|pcnet|virtio) ;;
    *) echo "invalid NIC model: $nic_model" >&2; usage ;;
esac
if $require_network && [[ "$nic_model" == "none" ]]; then
    echo "--require-network needs --nic e1000, pcnet or virtio" >&2
    exit 2
fi
command -v qemu-system-x86_64 >/dev/null 2>&1 || {
    echo "qemu-system-x86_64 is required" >&2; exit 1; }
media="$(cd "$(dirname "$media")" && pwd)/$(basename "$media")"
[[ -f "$media" && -s "$media" ]] || { echo "media missing or empty: $media" >&2; exit 1; }

tmp="$(mktemp -d "${TMPDIR:-/tmp}/kurogane-uefi-smoke.XXXXXX")"
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

# Modern OVMF packages normally ship split CODE + VARS pflash images. Feeding
# OVMF_CODE_4M.fd through QEMU's legacy -bios path is not portable and fails on
# Ubuntu 24.04. Discover a matched pair and give VARS a private writable copy.
firmware_code="${OVMF_CODE:-}"
firmware_vars_template="${OVMF_VARS:-}"

if [[ -z "$firmware_code" ]]; then
    code_candidates=(
        /usr/share/OVMF/OVMF_CODE_4M.fd
        /usr/share/OVMF/OVMF_CODE.fd
        /usr/share/edk2/x64/OVMF_CODE.fd
        /usr/share/edk2/ovmf/OVMF_CODE.fd
    )
    if command -v brew >/dev/null 2>&1; then
        brew_qemu="$(brew --prefix qemu 2>/dev/null || true)"
        if [[ -n "$brew_qemu" ]]; then
            code_candidates+=("$brew_qemu/share/qemu/edk2-x86_64-code.fd")
        fi
    fi
    for candidate in "${code_candidates[@]}"; do
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
        */edk2-x86_64-code.fd)
            directory="$(dirname "$firmware_code")"
            for candidate in \
                "$directory/edk2-i386-vars.fd" \
                "$directory/edk2-x86_64-vars.fd"; do
                if [[ -f "$candidate" ]]; then
                    firmware_vars_template="$candidate"
                    break
                fi
            done
            ;;
    esac
fi

[[ -n "$firmware_code" && -f "$firmware_code" ]] || {
    echo "OVMF/edk2 x86-64 CODE firmware not found; set OVMF_CODE=/path/to/CODE.fd" >&2
    exit 1
}
[[ -n "$firmware_vars_template" && -f "$firmware_vars_template" ]] || {
    echo "matching writable OVMF VARS template not found; set OVMF_VARS=/path/to/VARS.fd" >&2
    exit 1
}

firmware_vars="$tmp/OVMF_VARS.fd"
cp "$firmware_vars_template" "$firmware_vars"

network_args=(-net none)
if [[ "$nic_model" != "none" ]]; then
    qemu_nic_model="$nic_model"
    if [[ "$nic_model" == "virtio" ]]; then
        qemu_nic_model="virtio-net-pci"
    fi
    network_args=(
        -netdev user,id=kurogane_net
        -device "$qemu_nic_model,netdev=kurogane_net,mac=52:54:00:4b:55:01"
    )
fi

media_args=()
if [[ "$media_kind" == "disk" ]]; then
    # The Foundation image is intentionally attached as a protected snapshot.
    # It contains the runnable system but no install.pkg, so qualification
    # exercises the live OS instead of accidentally entering the installer.
    media_args=(
        -drive "if=none,id=kurogane_system,format=raw,file=$media,snapshot=on,cache=writeback"
        -device ide-hd,drive=kurogane_system,bus=ide.0,bootindex=1
    )
else
    media_args=(
        -boot order=d,menu=off
        -cdrom "$media"
    )
fi

qemu-system-x86_64 \
    -machine q35,accel=tcg \
    -cpu max \
    -m 1024 \
    -drive if=pflash,format=raw,unit=0,readonly=on,file="$firmware_code" \
    -drive if=pflash,format=raw,unit=1,file="$firmware_vars" \
    "${media_args[@]}" \
    -serial "file:$serial" \
    -display none \
    "${network_args[@]}" \
    -no-reboot \
    -no-shutdown \
    >"$qemu_log" 2>&1 &
pid=$!

deadline=$((SECONDS + timeout_seconds))
while ((SECONDS < deadline)); do
    if [[ -f "$serial" ]]; then
        if $require_network; then
            if grep -Fq '[TEST] dhcp_lease: PASS' "$serial" &&
               grep -Fq '[TEST] network_gateway_icmp: PASS' "$serial" &&
               grep -Fq '[TEST] ALL_REQUIRED_TESTS_PASSED' "$serial"; then
                echo "[uefi-qemu] $media_kind/$nic_model DHCP/gateway network: PASS"
                echo "[uefi-qemu] firmware CODE: $firmware_code"
                echo "[uefi-qemu] firmware VARS: $firmware_vars_template"
                exit 0
            fi
            if grep -Eq '\[TEST\] (dhcp_lease|network_gateway_icmp|ALL_REQUIRED_TESTS_PASSED): FAIL' "$serial"; then
                echo "KuroganeOS reported a network/runtime qualification failure for $nic_model" >&2
                tail -n 180 "$serial" >&2 || true
                exit 1
            fi
        elif grep -Eq 'KuroganeOS kernel entry|\[TEST\] paging: PASS|KUROGANE OS' "$serial"; then
            echo "[uefi-qemu] $media_kind UEFI boot: PASS"
            echo "[uefi-qemu] firmware CODE: $firmware_code"
            echo "[uefi-qemu] firmware VARS: $firmware_vars_template"
            exit 0
        fi
    fi
    if ! kill -0 "$pid" >/dev/null 2>&1; then
        echo "QEMU exited before the required KuroganeOS marker" >&2
        [[ -f "$qemu_log" ]] && cat "$qemu_log" >&2 || true
        [[ -f "$serial" ]] && cat "$serial" >&2 || true
        exit 1
    fi
    sleep 1
done

if $require_network; then
    echo "OVMF/QEMU did not qualify $nic_model networking within $timeout_seconds seconds" >&2
else
    echo "OVMF/QEMU did not boot KuroganeOS $media_kind media within $timeout_seconds seconds" >&2
fi
[[ -f "$qemu_log" ]] && tail -n 100 "$qemu_log" >&2 || true
[[ -f "$serial" ]] && tail -n 180 "$serial" >&2 || true
exit 1

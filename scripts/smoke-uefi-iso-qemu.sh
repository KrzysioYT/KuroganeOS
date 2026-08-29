#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: ./scripts/smoke-uefi-iso-qemu.sh MEDIA [--disk] [--persistent-disk] [--timeout SECONDS] [--nic none|e1000|pcnet|virtio] [--require-network] [--require-tls] [--require-marker TEXT]" >&2
    exit 2
}

media=""
media_kind="iso"
persistent_disk=false
timeout_seconds=60
nic_model="none"
require_network=false
require_tls=false
require_marker=""
while (($#)); do
    case "$1" in
        --disk) media_kind="disk"; shift ;;
        --persistent-disk) persistent_disk=true; shift ;;
        --timeout) [[ $# -ge 2 ]] || usage; timeout_seconds="$2"; shift 2 ;;
        --nic) [[ $# -ge 2 ]] || usage; nic_model="$2"; shift 2 ;;
        --require-network) require_network=true; shift ;;
        --require-tls) require_tls=true; require_network=true; shift ;;
        --require-marker) [[ $# -ge 2 && -n "$2" ]] || usage; require_marker="$2"; shift 2 ;;
        -h|--help) usage ;;
        -*) usage ;;
        *) [[ -z "$media" ]] || usage; media="$1"; shift ;;
    esac
done
[[ -n "$media" ]] || usage
if $persistent_disk && [[ "$media_kind" != "disk" ]]; then
    echo "--persistent-disk requires --disk" >&2
    exit 2
fi
[[ "$timeout_seconds" =~ ^[0-9]+$ ]] && ((timeout_seconds >= 10 && timeout_seconds <= 240)) || {
    echo "invalid timeout" >&2; exit 2; }
case "$nic_model" in
    none|e1000|pcnet|virtio) ;;
    *) echo "invalid NIC model: $nic_model" >&2; usage ;;
esac
if $require_network && [[ "$nic_model" == "none" ]]; then
    echo "--require-network/--require-tls needs --nic e1000, pcnet or virtio" >&2
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
    if $persistent_disk; then
        media_args=(
            -drive "if=none,id=kurogane_system,format=raw,file=$media,cache=directsync"
            -device ide-hd,drive=kurogane_system,bus=ide.0,bootindex=1
        )
    else
        media_args=(
            -drive "if=none,id=kurogane_system,format=raw,file=$media,snapshot=on,cache=writeback"
            -device ide-hd,drive=kurogane_system,bus=ide.0,bootindex=1
        )
    fi
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

failure_marker=""
if [[ -n "$require_marker" && "$require_marker" == *": PASS" ]]; then
    failure_marker="${require_marker%: PASS}: FAIL"
fi

deadline=$((SECONDS + timeout_seconds))
while ((SECONDS < deadline)); do
    if [[ -f "$serial" ]]; then
        if [[ -n "$require_marker" ]]; then
            if grep -Fq "$require_marker" "$serial"; then
                echo "[uefi-qemu] required runtime marker: PASS"
                echo "[uefi-qemu] marker: $require_marker"
                echo "[uefi-qemu] firmware CODE: $firmware_code"
                echo "[uefi-qemu] firmware VARS: $firmware_vars_template"
                exit 0
            fi
            if [[ -n "$failure_marker" ]] && grep -Fq "$failure_marker" "$serial"; then
                echo "KuroganeOS reported failure for required runtime marker" >&2
                tail -n 220 "$serial" >&2 || true
                exit 1
            fi
        elif $require_network; then
            if grep -Fq '[TEST] installer_package_preflight: PASS' "$serial"; then
                echo "QEMU network qualification received installer/setup media instead of a Foundation/live image." >&2
                echo "The guest entered INSTALLER MODE before the normal network runtime could start." >&2
                echo "Use a disk image without install.pkg (Windows: build/images/KuroganeOS-base.img; Linux: build/images/KuroganeOS-linux.img)." >&2
                tail -n 80 "$serial" >&2 || true
                exit 2
            fi

            if $require_tls && grep -Fq '[TEST] tls_https_optional: SKIP' "$serial"; then
                echo "KuroganeOS reached networking but real TLS/HTTPS qualification did not pass for $nic_model" >&2
                tail -n 220 "$serial" >&2 || true
                exit 1
            fi

            network_ready=false
            if grep -Fq '[TEST] dhcp_lease: PASS' "$serial" &&
               grep -Fq '[TEST] network_gateway_icmp: PASS' "$serial" &&
               grep -Fq '[TEST] ALL_REQUIRED_TESTS_PASSED' "$serial"; then
                network_ready=true
            fi

            tls_ready=true
            if $require_tls; then
                tls_ready=false
                if grep -Fq '[TEST] tls_https_optional: PASS' "$serial"; then
                    tls_ready=true
                fi
            fi

            if $network_ready && $tls_ready; then
                echo "[uefi-qemu] $media_kind/$nic_model DHCP/gateway network: PASS"
                if $require_tls; then
                    echo "[uefi-qemu] $media_kind/$nic_model real TLS/HTTPS handshake: PASS"
                fi
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

if [[ -n "$require_marker" ]]; then
    echo "OVMF/QEMU did not observe required marker within $timeout_seconds seconds: $require_marker" >&2
elif $require_tls; then
    echo "OVMF/QEMU did not qualify $nic_model TLS/HTTPS within $timeout_seconds seconds" >&2
elif $require_network; then
    echo "OVMF/QEMU did not qualify $nic_model networking within $timeout_seconds seconds" >&2
else
    echo "OVMF/QEMU did not boot KuroganeOS $media_kind media within $timeout_seconds seconds" >&2
fi
[[ -f "$qemu_log" ]] && tail -n 100 "$qemu_log" >&2 || true
[[ -f "$serial" ]] && tail -n 220 "$serial" >&2 || true
exit 1

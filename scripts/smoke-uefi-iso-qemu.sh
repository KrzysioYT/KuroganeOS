#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: ./scripts/smoke-uefi-iso-qemu.sh MEDIA [--disk] [--persistent-disk] [--accel tcg|kvm] [--timeout SECONDS] [--nic none|e1000|pcnet|virtio] [--audio none|ac97] [--require-network] [--require-tls] [--send-key-after-marker TEXT KEY] [--send-key-after-marker-count TEXT COUNT KEY ...] [--click-after-marker TEXT X Y ...] [--require-marker TEXT ...] [--require-marker-count TEXT COUNT ...]" >&2
    exit 2
}

media=""
media_kind="iso"
persistent_disk=false
accel_model="tcg"
timeout_seconds=60
nic_model="none"
audio_model="none"
require_network=false
require_tls=false
send_key_after_marker=""
send_key_name=""
send_key_sent=false
send_key_count_markers=()
send_key_count_targets=()
send_key_count_names=()
send_key_count_sent=()
click_markers=()
click_x=()
click_y=()
click_sent=()
require_markers=()
require_marker_count_markers=()
require_marker_count_targets=()
while (($#)); do
    case "$1" in
        --disk) media_kind="disk"; shift ;;
        --persistent-disk) persistent_disk=true; shift ;;
        --accel) [[ $# -ge 2 ]] || usage; accel_model="$2"; shift 2 ;;
        --timeout) [[ $# -ge 2 ]] || usage; timeout_seconds="$2"; shift 2 ;;
        --nic) [[ $# -ge 2 ]] || usage; nic_model="$2"; shift 2 ;;
        --audio) [[ $# -ge 2 ]] || usage; audio_model="$2"; shift 2 ;;
        --require-network) require_network=true; shift ;;
        --require-tls) require_tls=true; require_network=true; shift ;;
        --send-key-after-marker)
            [[ $# -ge 3 && -n "$2" && -n "$3" ]] || usage
            [[ -z "$send_key_after_marker" ]] || { echo "only one --send-key-after-marker is supported" >&2; exit 2; }
            send_key_after_marker="$2"
            send_key_name="$3"
            shift 3
            ;;
        --send-key-after-marker-count)
            [[ $# -ge 4 && -n "$2" && "$3" =~ ^[1-9][0-9]*$ && -n "$4" ]] || usage
            send_key_count_markers+=("$2")
            send_key_count_targets+=("$3")
            send_key_count_names+=("$4")
            send_key_count_sent+=(false)
            shift 4
            ;;
        --click-after-marker)
            [[ $# -ge 4 && -n "$2" && "$3" =~ ^[0-9]+$ && "$4" =~ ^[0-9]+$ ]] || usage
            click_markers+=("$2")
            click_x+=("$3")
            click_y+=("$4")
            click_sent+=(false)
            shift 4
            ;;
        --require-marker) [[ $# -ge 2 && -n "$2" ]] || usage; require_markers+=("$2"); shift 2 ;;
        --require-marker-count)
            [[ $# -ge 3 && -n "$2" && "$3" =~ ^[1-9][0-9]*$ ]] || usage
            require_marker_count_markers+=("$2")
            require_marker_count_targets+=("$3")
            shift 3
            ;;
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
case "$accel_model" in
    tcg) cpu_model="max" ;;
    kvm)
        [[ -c /dev/kvm && -r /dev/kvm && -w /dev/kvm ]] || {
            echo "--accel kvm requires read/write access to /dev/kvm" >&2
            exit 1
        }
        cpu_model="host"
        ;;
    *) echo "invalid accelerator: $accel_model" >&2; usage ;;
esac
case "$audio_model" in
    none|ac97) ;;
    *) echo "invalid audio model: $audio_model" >&2; usage ;;
esac
if [[ -n "$send_key_name" && ! "$send_key_name" =~ ^[A-Za-z0-9_-]+$ ]]; then
    echo "invalid QEMU sendkey name: $send_key_name" >&2
    exit 2
fi
for key in "${send_key_count_names[@]}"; do
    if [[ ! "$key" =~ ^[A-Za-z0-9_-]+$ ]]; then
        echo "invalid QEMU sendkey name: $key" >&2
        exit 2
    fi
done
for index in "${!click_markers[@]}"; do
    if (( click_x[index] > 4095 || click_y[index] > 4095 )); then
        echo "invalid QEMU click coordinate: ${click_x[$index]},${click_y[$index]}" >&2
        exit 2
    fi
done
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
monitor="$tmp/qemu-monitor.sock"
qmp="$tmp/qemu-qmp.sock"
pid=""
cleanup() {
    if [[ -n "$pid" ]] && kill -0 "$pid" >/dev/null 2>&1; then
        kill "$pid" >/dev/null 2>&1 || true
        wait "$pid" >/dev/null 2>&1 || true
    fi
    rm -rf -- "$tmp"
}
trap cleanup EXIT INT TERM

marker_occurrences() {
    local marker="$1"
    local count
    count="$(grep -F -c -- "$marker" "$serial" 2>/dev/null || true)"
    printf '%s\n' "${count:-0}"
}

send_qemu_key() {
    local key="$1"
    python3 - "$qmp" "$key" <<'PY'
import json
import socket
import sys
import time

path, key = sys.argv[1], sys.argv[2]
last_error = None


def receive_object(stream, operation):
    while True:
        line = stream.readline()
        if not line:
            raise RuntimeError(f"QMP disconnected during {operation}")
        message = json.loads(line.decode("utf-8"))
        if "event" in message:
            continue
        if "error" in message:
            error = message["error"]
            raise RuntimeError(
                f"QMP {operation} failed: {error.get('class', 'Error')}: "
                f"{error.get('desc', error)}")
        if "return" in message:
            return message["return"]


for _ in range(40):
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        client.settimeout(2.0)
        client.connect(path)
        stream = client.makefile("rwb", buffering=0)
        greeting_line = stream.readline()
        if not greeting_line:
            raise RuntimeError("QMP greeting missing")
        greeting = json.loads(greeting_line.decode("utf-8"))
        if "QMP" not in greeting:
            raise RuntimeError(f"invalid QMP greeting: {greeting}")

        stream.write(json.dumps({"execute": "qmp_capabilities"}).encode("utf-8") + b"\r\n")
        receive_object(stream, "qmp_capabilities")
        stream.write(json.dumps({
            "execute": "send-key",
            "arguments": {
                "keys": [{"type": "qcode", "data": key}],
                "hold-time": 80,
            },
        }).encode("utf-8") + b"\r\n")
        receive_object(stream, f"send-key {key}")
        stream.close()
        client.close()
        print(f"[qmp] acknowledged send-key {key}")
        raise SystemExit(0)
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        last_error = error
        try:
            client.close()
        except OSError:
            pass
        time.sleep(0.05)
raise SystemExit(f"cannot send QEMU key through QMP: {last_error}")
PY
}

send_qemu_click() {
    local target_x="$1"
    local target_y="$2"
    python3 - "$qmp" "$target_x" "$target_y" <<'PY'
import json
import socket
import sys
import time

path = sys.argv[1]
target_x = int(sys.argv[2])
target_y = int(sys.argv[3])
last_error = None


def receive_object(stream, operation):
    while True:
        line = stream.readline()
        if not line:
            raise RuntimeError(f"QMP disconnected during {operation}")
        message = json.loads(line.decode("utf-8"))
        if "event" in message:
            continue
        if "error" in message:
            error = message["error"]
            raise RuntimeError(
                f"QMP {operation} failed: {error.get('class', 'Error')}: "
                f"{error.get('desc', error)}")
        if "return" in message:
            return message["return"]


def execute(stream, name, arguments=None):
    payload = {"execute": name}
    if arguments is not None:
        payload["arguments"] = arguments
    stream.write(json.dumps(payload).encode("utf-8") + b"\r\n")
    receive_object(stream, name)


def relative(stream, dx, dy):
    events = []
    if dx:
        events.append({"type": "rel", "data": {"axis": "x", "value": dx}})
    if dy:
        events.append({"type": "rel", "data": {"axis": "y", "value": dy}})
    if events:
        execute(stream, "input-send-event", {"events": events})


def move_axis(stream, axis, amount):
    remaining = amount
    while remaining:
        step = max(-100, min(100, remaining))
        relative(stream, step if axis == "x" else 0, step if axis == "y" else 0)
        remaining -= step


for _ in range(40):
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        client.settimeout(2.0)
        client.connect(path)
        stream = client.makefile("rwb", buffering=0)
        greeting = json.loads(stream.readline().decode("utf-8"))
        if "QMP" not in greeting:
            raise RuntimeError(f"invalid QMP greeting: {greeting}")
        execute(stream, "qmp_capabilities")

        # Kurogane's pointer state is clamped. Drive far negative first so the
        # resulting guest coordinate is deterministically (0,0), independent
        # of the firmware/guest cursor history, then walk to the target.
        for _ in range(50):
            relative(stream, -100, -100)
        move_axis(stream, "x", target_x)
        move_axis(stream, "y", target_y)
        execute(stream, "input-send-event", {"events": [
            {"type": "btn", "data": {"down": True, "button": "left"}}
        ]})
        time.sleep(0.08)
        execute(stream, "input-send-event", {"events": [
            {"type": "btn", "data": {"down": False, "button": "left"}}
        ]})
        time.sleep(0.35)
        stream.close()
        client.close()
        print(f"[qmp] clicked guest pointer at {target_x},{target_y}")
        raise SystemExit(0)
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        last_error = error
        try:
            client.close()
        except OSError:
            pass
        time.sleep(0.05)
raise SystemExit(f"cannot click through QMP: {last_error}")
PY
}

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

audio_args=()
if [[ "$audio_model" == "ac97" ]]; then
    # The null backend keeps CI headless while still exposing a real PCI
    # Intel AC97 device to KuroganeOS. Guest DMA/mixer programming remains real.
    audio_args=(
        -audiodev none,id=kurogane_audio
        -device AC97,audiodev=kurogane_audio
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
    -machine "q35,accel=$accel_model" \
    -cpu "$cpu_model" \
    -m 1024 \
    -drive if=pflash,format=raw,unit=0,readonly=on,file="$firmware_code" \
    -drive if=pflash,format=raw,unit=1,file="$firmware_vars" \
    "${media_args[@]}" \
    -serial "file:$serial" \
    -monitor "unix:$monitor,server,nowait" \
    -qmp "unix:$qmp,server=on,wait=off" \
    -display none \
    "${network_args[@]}" \
    "${audio_args[@]}" \
    -no-reboot \
    -no-shutdown \
    >"$qemu_log" 2>&1 &
pid=$!

failure_markers=()
for require_marker in "${require_markers[@]}"; do
    failure_marker=""
    if [[ "$require_marker" == *": PASS" ]]; then
        failure_marker="${require_marker%: PASS}: FAIL"
    fi
    failure_markers+=("$failure_marker")
done

deadline=$((SECONDS + timeout_seconds))
while ((SECONDS < deadline)); do
    if [[ -f "$serial" ]]; then
        if [[ -n "$send_key_after_marker" && "$send_key_sent" == false ]] &&
           grep -Fq "$send_key_after_marker" "$serial"; then
            send_qemu_key "$send_key_name"
            send_key_sent=true
            echo "[uefi-qemu] sent key '$send_key_name' after marker: $send_key_after_marker"
        fi
        for index in "${!send_key_count_markers[@]}"; do
            if [[ "${send_key_count_sent[$index]}" == true ]]; then
                continue
            fi
            marker="${send_key_count_markers[$index]}"
            target="${send_key_count_targets[$index]}"
            if (( $(marker_occurrences "$marker") >= target )); then
                key="${send_key_count_names[$index]}"
                send_qemu_key "$key"
                send_key_count_sent[$index]=true
                echo "[uefi-qemu] sent key '$key' after marker occurrence $target: $marker"
            fi
        done
        for index in "${!click_markers[@]}"; do
            if [[ "${click_sent[$index]}" == true ]]; then
                continue
            fi
            marker="${click_markers[$index]}"
            if grep -Fq "$marker" "$serial"; then
                send_qemu_click "${click_x[$index]}" "${click_y[$index]}"
                click_sent[$index]=true
                echo "[uefi-qemu] clicked ${click_x[$index]},${click_y[$index]} after marker: $marker"
            fi
        done
        all_markers_ready=true
        if ((${#require_markers[@]} != 0 || ${#require_marker_count_markers[@]} != 0)); then
            for index in "${!require_markers[@]}"; do
                require_marker="${require_markers[$index]}"
                failure_marker="${failure_markers[$index]}"
                if [[ -n "$failure_marker" ]] && grep -Fq "$failure_marker" "$serial"; then
                    echo "KuroganeOS reported failure for required runtime marker: $require_marker" >&2
                    tail -n 220 "$serial" >&2 || true
                    exit 1
                fi
                if ! grep -Fq "$require_marker" "$serial"; then
                    all_markers_ready=false
                fi
            done
            for index in "${!require_marker_count_markers[@]}"; do
                marker="${require_marker_count_markers[$index]}"
                target="${require_marker_count_targets[$index]}"
                if (( $(marker_occurrences "$marker") < target )); then
                    all_markers_ready=false
                fi
            done
            if $all_markers_ready && ! $require_network; then
                echo "[uefi-qemu] required runtime markers: PASS"
                for require_marker in "${require_markers[@]}"; do
                    echo "[uefi-qemu] marker: $require_marker"
                done
                for index in "${!require_marker_count_markers[@]}"; do
                    echo "[uefi-qemu] marker count: ${require_marker_count_markers[$index]} >= ${require_marker_count_targets[$index]}"
                done
                echo "[uefi-qemu] firmware CODE: $firmware_code"
                echo "[uefi-qemu] firmware VARS: $firmware_vars_template"
                exit 0
            fi
        fi
        if $require_network; then
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

            if $network_ready && $tls_ready && $all_markers_ready; then
                if ((${#require_markers[@]} != 0 || ${#require_marker_count_markers[@]} != 0)); then
                    echo "[uefi-qemu] required runtime markers: PASS"
                    for require_marker in "${require_markers[@]}"; do
                        echo "[uefi-qemu] marker: $require_marker"
                    done
                    for index in "${!require_marker_count_markers[@]}"; do
                        echo "[uefi-qemu] marker count: ${require_marker_count_markers[$index]} >= ${require_marker_count_targets[$index]}"
                    done
                fi
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
        elif ((${#require_markers[@]} == 0 && ${#require_marker_count_markers[@]} == 0)) &&
             grep -Eq 'KuroganeOS kernel entry|\[TEST\] paging: PASS|KUROGANE OS' "$serial"; then
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

if ((${#require_markers[@]} != 0)); then
    echo "OVMF/QEMU did not satisfy all requested runtime requirements within $timeout_seconds seconds." >&2
    echo "Required markers:" >&2
    for require_marker in "${require_markers[@]}"; do
        echo "  $require_marker" >&2
    done
    if $require_network; then
        echo "Required network: DHCP lease, gateway ICMP and complete runtime" >&2
    fi
    if $require_tls; then
        echo "Required TLS: real TLS/HTTPS handshake" >&2
    fi
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

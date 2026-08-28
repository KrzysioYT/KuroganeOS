#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: ./scripts/capture-qemu-screenshots.sh IMAGE [OUTPUT_DIR]" >&2
    exit 2
}

image="${1:-}"
output_dir="${2:-build/screenshots}"
[[ -n "$image" ]] || usage
command -v qemu-system-x86_64 >/dev/null 2>&1 || { echo "qemu-system-x86_64 is required" >&2; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "python3 is required" >&2; exit 1; }
command -v convert >/dev/null 2>&1 || { echo "ImageMagick convert is required" >&2; exit 1; }

image="$(cd "$(dirname "$image")" && pwd)/$(basename "$image")"
[[ -f "$image" && -s "$image" ]] || { echo "image missing or empty: $image" >&2; exit 1; }
mkdir -p "$output_dir"
output_dir="$(cd "$output_dir" && pwd)"
rm -f "$output_dir"/*.ppm "$output_dir"/*.png "$output_dir"/serial.log "$output_dir"/qemu.log 2>/dev/null || true

tmp="$(mktemp -d "${TMPDIR:-/tmp}/kurogane-screens.XXXXXX")"
serial="$tmp/serial.log"
qemu_log="$tmp/qemu.log"
monitor_port=45557
pid=""

cleanup() {
    if [[ -n "$pid" ]] && kill -0 "$pid" >/dev/null 2>&1; then
        kill "$pid" >/dev/null 2>&1 || true
        wait "$pid" >/dev/null 2>&1 || true
    fi
    [[ -f "$serial" ]] && cp "$serial" "$output_dir/serial.log" || true
    [[ -f "$qemu_log" ]] && cp "$qemu_log" "$output_dir/qemu.log" || true
    rm -rf -- "$tmp"
}
trap cleanup EXIT INT TERM

firmware_code="${OVMF_CODE:-}"
firmware_vars_template="${OVMF_VARS:-}"
if [[ -z "$firmware_code" ]]; then
    for candidate in \
        /usr/share/OVMF/OVMF_CODE_4M.fd \
        /usr/share/OVMF/OVMF_CODE.fd \
        /usr/share/edk2/x64/OVMF_CODE.fd \
        /usr/share/edk2/ovmf/OVMF_CODE.fd; do
        if [[ -f "$candidate" ]]; then firmware_code="$candidate"; break; fi
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
[[ -n "$firmware_code" && -f "$firmware_code" ]] || { echo "OVMF CODE not found" >&2; exit 1; }
[[ -n "$firmware_vars_template" && -f "$firmware_vars_template" ]] || { echo "OVMF VARS not found" >&2; exit 1; }
firmware_vars="$tmp/OVMF_VARS.fd"
cp "$firmware_vars_template" "$firmware_vars"

qemu-system-x86_64 \
    -machine q35,accel=tcg \
    -cpu max \
    -m 1024 \
    -drive if=pflash,format=raw,unit=0,readonly=on,file="$firmware_code" \
    -drive if=pflash,format=raw,unit=1,file="$firmware_vars" \
    -drive "if=none,id=kurogane_system,format=raw,file=$image,snapshot=on,cache=writeback" \
    -device ide-hd,drive=kurogane_system,bus=ide.0,bootindex=1 \
    -netdev user,id=kurogane_net \
    -device e1000,netdev=kurogane_net,mac=52:54:00:4b:55:51 \
    -serial "file:$serial" \
    -monitor "tcp:127.0.0.1:$monitor_port,server=on,wait=off" \
    -display none \
    -no-reboot \
    -no-shutdown \
    >"$qemu_log" 2>&1 &
pid=$!

hmp() {
    python3 - "$monitor_port" "$@" <<'PY'
import socket, sys, time
port = int(sys.argv[1])
commands = sys.argv[2:]
last = None
for _ in range(100):
    try:
        last = socket.create_connection(("127.0.0.1", port), timeout=1.0)
        break
    except OSError:
        time.sleep(0.05)
if last is None:
    raise SystemExit("could not connect to QEMU HMP monitor")
with last as sock:
    sock.settimeout(0.2)
    try:
        sock.recv(4096)
    except OSError:
        pass
    for command in commands:
        sock.sendall((command + "\n").encode("ascii"))
        time.sleep(0.12)
PY
}

wait_marker() {
    local marker="$1"
    local timeout_seconds="${2:-90}"
    local deadline=$((SECONDS + timeout_seconds))
    while ((SECONDS < deadline)); do
        if [[ -f "$serial" ]] && grep -Fq "$marker" "$serial"; then
            echo "[capture] marker: $marker"
            return 0
        fi
        if ! kill -0 "$pid" >/dev/null 2>&1; then
            echo "QEMU exited before marker: $marker" >&2
            [[ -f "$qemu_log" ]] && tail -n 120 "$qemu_log" >&2 || true
            [[ -f "$serial" ]] && tail -n 180 "$serial" >&2 || true
            return 1
        fi
        sleep 1
    done
    echo "timeout waiting for marker: $marker" >&2
    [[ -f "$serial" ]] && tail -n 180 "$serial" >&2 || true
    return 1
}

capture() {
    local name="$1"
    local ppm="$tmp/$name.ppm"
    local png="$output_dir/$name.png"
    hmp "screendump $ppm"
    for _ in $(seq 1 30); do
        [[ -s "$ppm" ]] && break
        sleep 0.1
    done
    [[ -s "$ppm" ]] || { echo "QEMU did not create screenshot: $name" >&2; return 1; }
    convert "$ppm" "$png"
    [[ -s "$png" ]] || { echo "PNG conversion failed: $name" >&2; return 1; }
    echo "[capture] $png"
}

send_key() {
    hmp "sendkey $1 ${2:-80}"
    sleep "${3:-0.35}"
}

# 1) Real Secure Access surface.
wait_marker '[TEST] kurogane5_obsidian_login: PASS' 120
sleep 1
capture '01-secure-access'

# 2) Enter the graphical session, then restore Blade explicitly.
send_key ret 90 0.8
wait_marker '[TEST] kurogane5_login_to_desktop: PASS' 60
wait_marker '[TEST] kurogane5_blade_launcher: PASS' 60
send_key meta_l 90 0.8
capture '02-blade-launcher'

# 3) Launch the real Ring-3 Kurosh through Blade's public hotkey.
send_key t 90 0.8
wait_marker '[TEST] kurogane5_kurosh_terminal: PASS' 45
capture '03-kurosh'

# 4) Restore Blade and open Kurogane Web. Wait for real HTTPS navigation when available.
send_key meta_l 90 0.7
send_key b 90 0.8
wait_marker '/gui/browser' 45
wait_marker '[TEST] desktop_browser_ring3: PASS' 45
if wait_marker '[TEST] kurogane5_web_initial_navigation: PASS' 75; then
    sleep 1
else
    echo "[capture] Web shell opened but HTTPS home did not finish before screenshot" >&2
fi
capture '04-kurogane-web'

# 5) Restore Blade and open Anvil. The launch marker proves this is the real child process.
send_key meta_l 90 0.7
send_key i 90 0.8
wait_marker '/gui/anvil' 45
sleep 4
capture '05-anvil'

# 6) Capture the live Performance application as a final runtime frame.
send_key meta_l 90 0.7
send_key v 90 0.8
wait_marker '/gui/perf' 45
sleep 2
capture '06-performance'

cp "$serial" "$output_dir/serial.log"
cp "$qemu_log" "$output_dir/qemu.log"
echo "[capture] REAL_QEMU_SCREENSHOTS_PASS"

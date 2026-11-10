#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image=""
timeout_seconds=45
display=false
keep=false
fullscreen=true
build_if_missing=false

usage() {
    cat >&2 <<'EOF'
usage: ./scripts/run-qemu-macos.sh [options]
  --image FILE        raw GPT image (default: newest dist/*-macos-qemu.img)
  --build-if-missing  explicitly build a debug image when none exists
  --timeout SECONDS   smoke-test timeout (default: 45; headless mode only)
  --display           show a large Cocoa QEMU window; fullscreen + zoom-to-fit
  --windowed          show a resizable Cocoa window with zoom-to-fit
  --fullscreen        explicitly use fullscreen Cocoa display
  --keep              leave headless QEMU running after successful smoke markers
EOF
    exit 2
}
while (($#)); do
    case "$1" in
        --image) image="${2:-}"; shift 2 ;;
        --build-if-missing) build_if_missing=true; shift ;;
        --timeout) timeout_seconds="${2:-}"; shift 2 ;;
        --display) display=true; keep=true; fullscreen=true; shift ;;
        --windowed) display=true; keep=true; fullscreen=false; shift ;;
        --fullscreen) display=true; keep=true; fullscreen=true; shift ;;
        --keep) keep=true; shift ;;
        *) usage ;;
    esac
done
[[ "$timeout_seconds" =~ ^[0-9]+$ ]] && ((timeout_seconds >= 5 && timeout_seconds <= 600)) || usage

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "run-qemu-macos.sh requires macOS" >&2
    exit 1
fi
command -v qemu-system-x86_64 >/dev/null 2>&1 || {
    echo "qemu-system-x86_64 is missing; run ./scripts/setup-macos.sh --install" >&2; exit 1; }

if [[ -z "$image" ]]; then
    image="$(ls -t "$root"/dist/KuroganeOS-*-macos-qemu.img 2>/dev/null | head -n 1 || true)"
    if [[ -z "$image" && "$build_if_missing" == true ]]; then
        echo "[qemu-macos] no built image found; explicit --build-if-missing requested"
        bash "$root/scripts/build-macos.sh" --configuration debug
        image="$(ls -t "$root"/dist/KuroganeOS-*-macos-qemu.img 2>/dev/null | head -n 1 || true)"
    fi
fi
if [[ -z "$image" ]]; then
    echo "QEMU image not found in dist/. Build it first with:" >&2
    echo "  ./scripts/build-macos.sh --rebuild --configuration debug" >&2
    echo "or explicitly allow the runner to build with --build-if-missing." >&2
    exit 1
fi
[[ -f "$image" ]] || { echo "QEMU image not found: $image" >&2; exit 1; }
image="$(cd "$(dirname "$image")" && pwd)/$(basename "$image")"

qemu_prefix=""
if command -v brew >/dev/null 2>&1; then
    qemu_prefix="$(brew --prefix qemu 2>/dev/null || true)"
fi
search_dirs=(
    "$qemu_prefix/share/qemu"
    "/opt/homebrew/share/qemu"
    "/usr/local/share/qemu"
)
firmware=""; vars=""
for dir in "${search_dirs[@]}"; do
    [[ -d "$dir" ]] || continue
    for candidate in edk2-x86_64-code.fd edk2-i386-code.fd; do
        [[ -f "$dir/$candidate" ]] && { firmware="$dir/$candidate"; break 2; }
    done
done
for dir in "${search_dirs[@]}"; do
    [[ -d "$dir" ]] || continue
    for candidate in edk2-i386-vars.fd edk2-x86_64-vars.fd; do
        [[ -f "$dir/$candidate" ]] && { vars="$dir/$candidate"; break 2; }
    done
done
[[ -n "$firmware" && -n "$vars" ]] || {
    echo "QEMU EDK2 firmware was not found under the Homebrew QEMU prefix" >&2; exit 1; }

mkdir -p "$root/build/logs" "$root/build/qemu-macos"
serial="$root/build/logs/qemu-macos-serial.log"
stdout_log="$root/build/logs/qemu-macos-stdout.log"
stderr_log="$root/build/logs/qemu-macos-stderr.log"
vars_copy="$root/build/qemu-macos/edk2-vars.fd"
qmp_socket="$root/build/qemu-macos/qmp.sock"
cp "$vars" "$vars_copy"
rm -f -- "$qmp_socket"
: > "$serial"; : > "$stdout_log"; : > "$stderr_log"

args=(
    -machine q35,accel=tcg
    -cpu max
    -m 768
    -vga std
    -drive "if=pflash,format=raw,unit=0,readonly=on,file=$firmware"
    -drive "if=pflash,format=raw,unit=1,file=$vars_copy"
    -drive "if=none,id=kurogane_system,format=raw,file=$image,snapshot=on,cache=writeback"
    -device ide-hd,drive=kurogane_system,bus=ide.0,bootindex=1
    -qmp "unix:$qmp_socket,server=on,wait=off"
    -serial "file:$serial"
    -netdev "user,id=kurogane_net,ipv4=on,ipv6=off,net=10.0.2.0/24,host=10.0.2.2,dhcpstart=10.0.2.15,dns=10.0.2.3"
    -device e1000,netdev=kurogane_net,mac=52:54:00:4b:55:01
    -audiodev coreaudio,id=kurogane_audio
    -device AC97,audiodev=kurogane_audio
    -no-reboot -no-shutdown
)
if $display; then
    # Cocoa's zoom-to-fit scales the guest framebuffer with the host window.
    # Do not force the host cursor: KuroganeOS renders its own software cursor.
    # Interactive display sessions deliberately do not use the smoke timeout:
    # forcibly terminating Cocoa while an NSEvent is being delivered can race
    # QEMU display/input teardown on macOS.
    args+=( -display "cocoa,zoom-to-fit=on" )
    if $fullscreen; then
        args+=( -full-screen )
    fi
else
    args+=( -display none )
fi

qemu-system-x86_64 "${args[@]}" >"$stdout_log" 2>"$stderr_log" &
pid=$!
cleanup() {
    if kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -f -- "$qmp_socket"
}
trap cleanup EXIT INT TERM

echo "[qemu-macos] PID $pid"
echo "[qemu-macos] image: $image"
echo "[qemu-macos] serial: $serial"
echo "[qemu-macos] audio: Intel AC97 -> CoreAudio"
echo "[qemu-macos] network: E1000 -> QEMU user IPv4 NAT (10.0.2.0/24)"
if $display; then
    if $fullscreen; then
        echo "[qemu-macos] display: Cocoa fullscreen + zoom-to-fit"
    else
        echo "[qemu-macos] display: Cocoa windowed + zoom-to-fit"
    fi
    echo "[qemu-macos] interactive mode: smoke timeout disabled; close QEMU to stop"
    set +e
    wait "$pid"
    qemu_status=$?
    set -e
    trap - EXIT INT TERM
    if ((qemu_status != 0)); then
        echo "[qemu-macos] QEMU exited with status $qemu_status" >&2
        tail -n 80 "$stderr_log" >&2 || true
    fi
    exit "$qemu_status"
fi

success=false
try_selected=false
try_login_activated=false
qmp_key() {
    python3 - "$qmp_socket" "$1" <<'PY'
import json
import socket
import sys
import time

path, key = sys.argv[1:3]
deadline = time.monotonic() + 10
connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
connection.settimeout(3)
while True:
    try:
        connection.connect(path)
        break
    except OSError:
        if time.monotonic() >= deadline:
            raise
        time.sleep(0.1)

def command(stream, identifier, name, arguments=None):
    request = {"execute": name, "id": identifier}
    if arguments is not None:
        request["arguments"] = arguments
    stream.write((json.dumps(request) + "\r\n").encode("utf-8"))
    while True:
        response = json.loads(stream.readline())
        if response.get("id") != identifier:
            continue
        if "error" in response:
            raise RuntimeError(f"QMP {name} failed: {response['error']}")
        return response

with connection, connection.makefile("rwb", buffering=0) as stream:
    greeting = json.loads(stream.readline())
    if "QMP" not in greeting:
        raise RuntimeError("QMP greeting is missing")
    command(stream, 1, "qmp_capabilities")
    command(stream, 2, "send-key", {
        "keys": [{"type": "qcode", "data": key}],
        "hold-time": 100,
    })
PY
}
for ((elapsed=0; elapsed<timeout_seconds*10; ++elapsed)); do
    if ! $try_selected &&
       grep -Fq 'installer: select setup mode:' "$serial" 2>/dev/null; then
        echo "[qemu-macos] setup media detected; selecting the non-destructive Try session"
        qmp_key ret
        try_selected=true
    fi
    if $try_selected && ! $try_login_activated &&
       grep -Fq '[TEST] red_flux_login_surface: PASS' "$serial" 2>/dev/null; then
        echo "[qemu-macos] live Login is visible; activating the non-password Try profile"
        qmp_key ret
        try_login_activated=true
    fi
    if grep -Eq '^\[(TEST|SESSION)\].*: FAIL\r?$|KERNEL PANIC|fatal:' "$serial" 2>/dev/null; then
        echo "[qemu-macos] runtime failure detected" >&2
        tail -n 100 "$serial" >&2 || true
        exit 1
    fi
    if grep -Fq '[TEST] desktop_session_fallback: PASS' "$serial" 2>/dev/null; then
        echo "[qemu-macos] Flux desktop fell back to the text console" >&2
        tail -n 100 "$serial" >&2 || true
        exit 1
    fi
    if grep -Fq '[TEST] userspace_init_spawn: PASS' "$serial" 2>/dev/null &&
       grep -Fq '[TEST] ALL_REQUIRED_TESTS_PASSED' "$serial" 2>/dev/null &&
       grep -Fq '[TEST] desktop_session: PASS' "$serial" 2>/dev/null &&
       grep -Fq '[TEST] red_flux_login_surface: PASS' "$serial" 2>/dev/null &&
       grep -Fq '[TEST] desktop_userspace_apps: PASS' "$serial" 2>/dev/null &&
       grep -Fq '[TEST] userspace_desktop_session: PASS' "$serial" 2>/dev/null &&
       { ! $try_selected ||
           { $try_login_activated &&
             grep -Fq '[TEST] live_package_root: PASS' "$serial" 2>/dev/null &&
             grep -Fq '[TEST] setup_try_mode: PASS' "$serial" 2>/dev/null &&
             grep -Fq '[TEST] red_flux_owned_session: PASS' "$serial" 2>/dev/null &&
             grep -Fq '[SESSION] flux_home_first_frame: PASS' "$serial" 2>/dev/null &&
             grep -Fq '[TEST] red_flux_home_surface: PASS' "$serial" 2>/dev/null; }; }; then
        success=true
        break
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "[qemu-macos] QEMU exited before the required markers" >&2
        tail -n 80 "$stderr_log" >&2 || true
        exit 1
    fi
    sleep 0.1
done

if ! $success; then
    echo "[qemu-macos] timed out after ${timeout_seconds}s" >&2
    tail -n 120 "$serial" >&2 || true
    exit 1
fi

echo "[qemu-macos] userspace_init_spawn: PASS"
echo "[qemu-macos] ALL_REQUIRED_TESTS_PASSED"
echo "[qemu-macos] desktop_session: PASS"
echo "[qemu-macos] red_flux_login_surface: PASS"
echo "[qemu-macos] desktop_userspace_apps: PASS"
echo "[qemu-macos] userspace_desktop_session: PASS"
if $try_selected; then
    echo "[qemu-macos] red_flux_owned_session: PASS"
    echo "[qemu-macos] flux_home_first_frame: PASS"
    echo "[qemu-macos] red_flux_home_surface: PASS"
fi
if $keep; then
    trap - EXIT INT TERM
    echo "[qemu-macos] leaving QEMU running (PID $pid)"
else
    cleanup
fi

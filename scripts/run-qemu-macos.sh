#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image=""
timeout_seconds=60
memory_mib=1024
display=false
keep=false
fullscreen=true
build_if_missing=false

usage() {
    cat >&2 <<'EOF'
usage: ./scripts/run-qemu-macos.sh [options]
  --image FILE        raw GPT image (default: newest dist/*-macos-qemu.img,
                      then state/KuroganeOS.img, then build/images/KuroganeOS-base.img)
  --build-if-missing  explicitly build a debug image when none exists
  --timeout SECONDS   smoke timeout (default: 60; headless only)
  --memory MIB        guest memory (default: 1024)
  --display           Cocoa fullscreen + zoom-to-fit
  --windowed          resizable Cocoa window + zoom-to-fit
  --fullscreen        explicitly use fullscreen Cocoa display
  --keep              leave headless QEMU running after successful markers
EOF
    exit 2
}

while (($#)); do
    case "$1" in
        --image) image="${2:-}"; shift 2 ;;
        --build-if-missing) build_if_missing=true; shift ;;
        --timeout) timeout_seconds="${2:-}"; shift 2 ;;
        --memory) memory_mib="${2:-}"; shift 2 ;;
        --display) display=true; keep=true; fullscreen=true; shift ;;
        --windowed) display=true; keep=true; fullscreen=false; shift ;;
        --fullscreen) display=true; keep=true; fullscreen=true; shift ;;
        --keep) keep=true; shift ;;
        *) usage ;;
    esac
done
[[ "$timeout_seconds" =~ ^[0-9]+$ ]] && ((timeout_seconds >= 5 && timeout_seconds <= 600)) || usage
[[ "$memory_mib" =~ ^[0-9]+$ ]] && ((memory_mib >= 256 && memory_mib <= 4096)) || usage

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "run-qemu-macos.sh requires macOS" >&2
    exit 1
fi
command -v qemu-system-x86_64 >/dev/null 2>&1 || {
    echo "qemu-system-x86_64 is missing; run ./scripts/setup-macos.sh --install" >&2
    exit 1
}

if [[ -z "$image" ]]; then
    image="$(ls -t "$root"/dist/KuroganeOS-*-macos-qemu.img 2>/dev/null | head -n 1 || true)"
    [[ -n "$image" ]] || [[ ! -f "$root/state/KuroganeOS.img" ]] || image="$root/state/KuroganeOS.img"
    [[ -n "$image" ]] || [[ ! -f "$root/build/images/KuroganeOS-base.img" ]] || image="$root/build/images/KuroganeOS-base.img"
    if [[ -z "$image" && "$build_if_missing" == true ]]; then
        echo "[qemu-macos] no image found; building debug media"
        bash "$root/scripts/build-macos.sh" --configuration debug --rebuild
        [[ ! -f "$root/build/images/KuroganeOS-base.img" ]] || image="$root/build/images/KuroganeOS-base.img"
    fi
fi
if [[ -z "$image" ]]; then
    echo "QEMU image not found. Build first with:" >&2
    echo "  ./scripts/build-macos.sh --configuration debug --rebuild" >&2
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
firmware=""
vars=""
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
    echo "QEMU EDK2 firmware was not found under the Homebrew QEMU prefix" >&2
    exit 1
}

mkdir -p "$root/build/logs" "$root/build/qemu-macos"
serial="$root/build/logs/qemu-macos-serial.log"
stdout_log="$root/build/logs/qemu-macos-stdout.log"
stderr_log="$root/build/logs/qemu-macos-stderr.log"
vars_copy="$root/build/qemu-macos/edk2-vars.fd"
cp "$vars" "$vars_copy"
: > "$serial"
: > "$stdout_log"
: > "$stderr_log"

args=(
    -machine q35,accel=tcg
    -cpu max
    -m "$memory_mib"
    -vga std
    -drive "if=pflash,format=raw,unit=0,readonly=on,file=$firmware"
    -drive "if=pflash,format=raw,unit=1,file=$vars_copy"
    -drive "if=none,id=kurogane_system,format=raw,file=$image,snapshot=on,cache=writeback"
    -device ide-hd,drive=kurogane_system,bus=ide.0,bootindex=1
    -serial "file:$serial"
    -netdev "user,id=kurogane_net,ipv4=on,ipv6=off,net=10.0.2.0/24,host=10.0.2.2,dhcpstart=10.0.2.15,dns=10.0.2.3"
    -device e1000,netdev=kurogane_net,mac=52:54:00:4b:55:01
    -audiodev coreaudio,id=kurogane_audio
    -device AC97,audiodev=kurogane_audio
    -no-reboot
    -no-shutdown
)
if $display; then
    args+=( -display "cocoa,zoom-to-fit=on" )
    $fullscreen && args+=( -full-screen )
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
}
trap cleanup EXIT INT TERM

echo "[qemu-macos] PID $pid"
echo "[qemu-macos] image: $image"
echo "[qemu-macos] memory: ${memory_mib} MiB"
echo "[qemu-macos] accelerator: TCG (x86-64 guest)"
echo "[qemu-macos] serial: $serial"
echo "[qemu-macos] audio: Intel AC97 -> CoreAudio"
echo "[qemu-macos] network: E1000 -> QEMU user IPv4 NAT"

if $display; then
    echo "[qemu-macos] display: Cocoa $( $fullscreen && echo fullscreen || echo windowed ) + zoom-to-fit"
    echo "[qemu-macos] interactive mode: close QEMU to stop"
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
for ((elapsed=0; elapsed<timeout_seconds*10; ++elapsed)); do
    if grep -Eq '^\[TEST\].*: FAIL\r?$|KERNEL PANIC|KERNEL EXCEPTION|fatal:' "$serial" 2>/dev/null; then
        echo "[qemu-macos] runtime failure detected" >&2
        tail -n 120 "$serial" >&2 || true
        exit 1
    fi
    if grep -Fq '[TEST] userspace_init_spawn: PASS' "$serial" 2>/dev/null &&
       grep -Fq '[TEST] ALL_REQUIRED_TESTS_PASSED' "$serial" 2>/dev/null &&
       grep -Fq '[TEST] desktop_session: PASS' "$serial" 2>/dev/null &&
       grep -Fq '[TEST] kurogane5_obsidian_login: PASS' "$serial" 2>/dev/null &&
       grep -Fq '[TEST] userspace_desktop_session: PASS' "$serial" 2>/dev/null; then
        success=true
        break
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "[qemu-macos] QEMU exited before required markers" >&2
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

echo "[qemu-macos] boot/runtime/login markers: PASS"
if $keep; then
    trap - EXIT INT TERM
    echo "[qemu-macos] leaving QEMU running (PID $pid)"
else
    cleanup
fi

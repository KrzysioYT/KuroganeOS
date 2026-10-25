#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: bash ./scripts/qualify-kurofs-runtime.sh SYSTEM_IMAGE KUROFS_IMAGE" >&2
    exit 2
}

[[ $# -eq 2 ]] || usage
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
system_image="$1"
kurofs_image="$2"
[[ -s "$system_image" ]] || { echo "missing system image: $system_image" >&2; exit 1; }
[[ -s "$kurofs_image" ]] || { echo "missing KuroFS image: $kurofs_image" >&2; exit 1; }

for tool in qemu-system-x86_64 sha256sum grep; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "missing runtime qualification tool: $tool" >&2
        exit 1
    }
done
test -c /dev/kvm && test -r /dev/kvm && test -w /dev/kvm || {
    echo "read-write /dev/kvm is required for native KuroFS qualification" >&2
    exit 1
}

firmware_code=""
for candidate in \
    /usr/share/OVMF/OVMF_CODE_4M.fd \
    /usr/share/OVMF/OVMF_CODE.fd; do
    if [[ -f "$candidate" ]]; then
        firmware_code="$candidate"
        break
    fi
done
[[ -n "$firmware_code" ]] || { echo "OVMF CODE firmware not found" >&2; exit 1; }
if [[ "$firmware_code" == */OVMF_CODE_4M.fd ]]; then
    firmware_vars_template="${firmware_code%/OVMF_CODE_4M.fd}/OVMF_VARS_4M.fd"
else
    firmware_vars_template="${firmware_code%/OVMF_CODE.fd}/OVMF_VARS.fd"
fi
[[ -f "$firmware_vars_template" ]] || {
    echo "matching OVMF VARS firmware not found" >&2
    exit 1
}

evidence_dir="${KUROFS_EVIDENCE_DIR:-$root/build/kurofs-runtime-evidence}"
mkdir -p "$evidence_dir"
rm -f -- \
    "$evidence_dir/boot1.serial.log" \
    "$evidence_dir/boot1.qemu.log" \
    "$evidence_dir/boot1.OVMF_VARS.fd" \
    "$evidence_dir/boot2.serial.log" \
    "$evidence_dir/boot2.qemu.log" \
    "$evidence_dir/boot2.OVMF_VARS.fd" \
    "$evidence_dir/kurofs-before.sha256" \
    "$evidence_dir/kurofs-after-boot1.sha256" \
    "$evidence_dir/kurofs-after-boot2.sha256"

qemu_pid=""
stop_vm() {
    if [[ -n "$qemu_pid" ]] && kill -0 "$qemu_pid" >/dev/null 2>&1; then
        # The guest has already completed ku_file_sync(). QEMU's TERM handler
        # closes and flushes the direct-sync backend before the next process.
        kill "$qemu_pid" >/dev/null 2>&1 || true
        for _ in $(seq 1 50); do
            kill -0 "$qemu_pid" >/dev/null 2>&1 || break
            sleep 0.1
        done
        if kill -0 "$qemu_pid" >/dev/null 2>&1; then
            kill -KILL "$qemu_pid" >/dev/null 2>&1 || true
        fi
        wait "$qemu_pid" >/dev/null 2>&1 || true
    fi
    qemu_pid=""
}
trap stop_vm EXIT INT TERM

run_boot() {
    local label="$1"
    local expected_marker="$2"
    local serial="$evidence_dir/$label.serial.log"
    local qemu_log="$evidence_dir/$label.qemu.log"
    local vars="$evidence_dir/$label.OVMF_VARS.fd"
    local passed=0

    : > "$serial"
    : > "$qemu_log"
    cp "$firmware_vars_template" "$vars"
    qemu-system-x86_64 \
        -machine q35,accel=kvm \
        -cpu host \
        -m 1024 \
        -drive "if=pflash,format=raw,unit=0,readonly=on,file=$firmware_code" \
        -drive "if=pflash,format=raw,unit=1,file=$vars" \
        -drive "if=none,id=kurogane_system,format=raw,file=$system_image,snapshot=on,cache=writeback" \
        -device ide-hd,drive=kurogane_system,bus=ide.0,bootindex=1 \
        -drive "if=none,id=kurogane_kurofs,format=raw,file=$kurofs_image,cache=directsync" \
        -device ide-hd,drive=kurogane_kurofs,bus=ide.1 \
        -netdev user,id=kurogane_net \
        -device e1000,netdev=kurogane_net,mac=52:54:00:4b:55:04 \
        -audiodev driver=none,id=kurogane_audio \
        -device AC97,audiodev=kurogane_audio \
        -serial "file:$serial" \
        -display none \
        -no-reboot \
        -no-shutdown \
        >"$qemu_log" 2>&1 &
    qemu_pid=$!

    for _ in $(seq 1 1200); do
        if grep -Eq '^\[TEST\].*: FAIL\r?$|KERNEL PANIC|fatal:|=== KUROGANE_FATAL_BEGIN ===' "$serial" 2>/dev/null; then
            break
        fi
        if grep -Fq '[TEST] kurofs_vfs_mount: PASS' "$serial" 2>/dev/null &&
           grep -Fq "$expected_marker" "$serial" 2>/dev/null; then
            passed=1
            break
        fi
        if ! kill -0 "$qemu_pid" >/dev/null 2>&1; then
            break
        fi
        sleep 0.2
    done

    stop_vm
    cat "$qemu_log"
    tail -n 12000 "$serial"
    if grep -Eq '^\[TEST\].*: FAIL\r?$|KERNEL PANIC|fatal:|=== KUROGANE_FATAL_BEGIN ===' "$serial"; then
        echo "$label emitted a runtime failure marker" >&2
        return 1
    fi
    [[ "$passed" -eq 1 ]] || {
        echo "$label did not reach the required KuroFS runtime state" >&2
        return 1
    }
    grep -F '[TEST] kurofs_vfs_mount: PASS' "$serial"
    grep -F "$expected_marker" "$serial"
}

sha256sum "$kurofs_image" > "$evidence_dir/kurofs-before.sha256"
run_boot boot1 '[TEST] kurofs_runtime_first_boot: PASS'
sha256sum "$kurofs_image" > "$evidence_dir/kurofs-after-boot1.sha256"
if cmp -s \
    "$evidence_dir/kurofs-before.sha256" \
    "$evidence_dir/kurofs-after-boot1.sha256"; then
    echo "first boot did not modify the dedicated KuroFS image" >&2
    exit 1
fi

# run_boot always starts a new QEMU process with fresh OVMF variables. The raw
# KuroFS image is intentionally neither recreated nor formatted between calls.
run_boot boot2 '[TEST] kurofs_runtime_second_boot: PASS'
sha256sum "$kurofs_image" > "$evidence_dir/kurofs-after-boot2.sha256"

echo '[TEST] kurofs_native_two_boot: PASS'

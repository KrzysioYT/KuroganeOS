#!/usr/bin/env bash
set -euo pipefail

if (($# < 1 || $# > 2)); then
    echo "usage: $0 MEDIA [tcg|kvm]" >&2
    exit 2
fi

media="$1"
accelerator="${2:-kvm}"
case "$accelerator" in
    tcg|kvm) ;;
    *) echo "invalid accelerator: $accelerator" >&2; exit 2 ;;
esac

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

bash ./scripts/smoke-uefi-iso-qemu.sh \
    "$media" \
    --disk \
    --accel "$accelerator" \
    --timeout 240 \
    --require-marker '[TEST] status_ipc_stale_handle: PASS' \
    --require-marker '[TEST] status_shm_stale_handle: PASS' \
    --require-marker '[TEST] status_event_stale_handle: PASS' \
    --require-marker '[TEST] status_socket_stale_handle: PASS' \
    --require-marker '[TEST] status_vfs_stale_handle: PASS' \
    --require-marker '[TEST] unified_status_runtime: PASS' \
    --require-marker '[TEST] unified_status_probe_exit: PASS' \
    --require-marker '[TEST] ALL_REQUIRED_TESTS_PASSED'

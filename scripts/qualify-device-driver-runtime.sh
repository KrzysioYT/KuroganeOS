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
    --require-marker '[TEST] device_claim: PASS' \
    --require-marker '[TEST] device_generation: PASS' \
    --require-marker '[TEST] device_unbind: PASS' \
    --require-marker '[TEST] device_remove_cleanup: PASS' \
    --require-marker '[TEST] device_stale_handle: PASS' \
    --require-marker '[TEST] device_failure_isolation: PASS' \
    --require-marker '[TEST] device_rebind: PASS' \
    --require-marker '[TEST] device_resource_boundary: PASS' \
    --require-marker '[TEST] driver_match: PASS' \
    --require-marker '[TEST] driver_attach: PASS' \
    --require-marker '[TEST] driver_fallback: PASS' \
    --require-marker '[TEST] driver_failure_cleanup: PASS' \
    --require-marker '[TEST] device_discovery_ring3: PASS' \
    --require-marker '[TEST] device_query_ring3: PASS' \
    --require-marker '[TEST] device_resource_boundary_ring3: PASS' \
    --require-marker '[TEST] device_stale_handle_ring3: PASS' \
    --require-marker '[TEST] device_ring3_runtime: PASS' \
    --require-marker '[TEST] device_ring3_probe_exit: PASS' \
    --require-marker '[TEST] ALL_REQUIRED_TESTS_PASSED'

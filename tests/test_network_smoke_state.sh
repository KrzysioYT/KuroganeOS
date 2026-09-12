#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../scripts/qualification/network-smoke-state.sh"
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
serial="$scratch/serial.log"
checks=0
expect_state() {
    local expected=$1 tls=${2:-false} actual=0
    kurogane_network_smoke_state "$serial" "$tls" || actual=$?
    [[ "$actual" == "$expected" ]] || {
        echo "network serial evaluator: expected $expected, got $actual" >&2
        exit 1
    }
    checks=$((checks + 1))
}
expect_state 1
printf '[TEST] dhcp_lease: PASS\n[TEST] network_gateway_icmp: PASS\n' > "$serial"
expect_state 1
printf '[TEST] ALL_REQUIRED_TESTS_PASSED: FAIL\n' >> "$serial"
expect_state 2
printf '[TEST] ALL_REQUIRED_TESTS_PASSED\n' >> "$serial"
expect_state 2 # Earlier failure cannot be hidden by later success.
printf '[TEST] dhcp_lease: PASS\r\n[TEST] network_gateway_icmp: PASS\r\n[TEST] ALL_REQUIRED_TESTS_PASSED\r\n' > "$serial"
expect_state 0
expect_state 1 true
printf '[TEST] tls_https_optional: PASS\n' >> "$serial"
expect_state 0 true
printf '[TEST] tls_https_optional: FAIL\n' >> "$serial"
expect_state 2 true
expect_state 0 false # Optional TLS is outside a network-only gate.
printf '[TEST] tls_https_optional: SKIP\n' > "$serial"
expect_state 2 true
for marker in dhcp_lease network_gateway_icmp; do
    printf '[TEST] dhcp_lease: PASS\n[TEST] network_gateway_icmp: PASS\n[TEST] ALL_REQUIRED_TESTS_PASSED\n[TEST] %s: FAIL\n' "$marker" > "$serial"
    expect_state 2
done
printf '[TEST] dhcp_lease: PASS\n[TEST] network_gateway_icmp: PASS\n[TEST] ALL_REQUIRED_TESTS_PASSED_EXTRA\n' > "$serial"
expect_state 1
echo "network smoke serial evaluation: PASS ($checks cases; parser tests only)"

# Invalid device properties are rejected before launching QEMU or opening media.
runner="$(dirname "${BASH_SOURCE[0]}")/../scripts/smoke-uefi-iso-qemu.sh"
for vectors in -1 2049 012 '1,x=2' 999999999999999999999999; do
    result=0
    bash "$runner" "$scratch/image.img" --nic virtio --virtio-vectors "$vectors" > "$scratch/cli.log" 2>&1 || result=$?
    if [[ "$result" != 2 ]] || ! grep -Fq -- '--virtio-vectors requires' "$scratch/cli.log"; then
        echo "invalid VirtIO vector count was not rejected: $vectors" >&2
        exit 1
    fi
done
result=0
bash "$runner" "$scratch/image.img" --nic e1000 --virtio-vectors 0 > "$scratch/cli.log" 2>&1 || result=$?
if [[ "$result" != 2 ]] || ! grep -Fq -- '--virtio-vectors requires' "$scratch/cli.log"; then
    echo "VirtIO-only option accepted for E1000" >&2
    exit 1
fi
echo "network smoke device-property rejection: PASS (6 cases)"

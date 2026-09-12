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

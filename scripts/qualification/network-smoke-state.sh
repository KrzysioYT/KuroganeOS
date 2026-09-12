#!/usr/bin/env bash
# Shared serial evaluator: 0 complete, 1 waiting, 2 explicit runtime failure.
# This parses evidence; it never generates guest success markers.
kurogane_network_smoke_state() {
    local serial=$1 require_tls=$2
    [[ -f "$serial" ]] || return 1
    # Failure wins even if a success marker appeared earlier in the same log.
    if grep -Eq '\[TEST\] (dhcp_lease|network_gateway_icmp|ALL_REQUIRED_TESTS_PASSED): FAIL' "$serial"; then
        return 2
    fi
    if [[ "$require_tls" == true ]] &&
        grep -Eq '\[TEST\] tls_https_optional: (FAIL|SKIP)' "$serial"; then
        return 2
    fi
    # End anchoring prevents ALL_REQUIRED_TESTS_PASSED: FAIL or incomplete
    # marker fragments from satisfying the successful end-of-boot contract.
    grep -Eq $'\\[TEST\\] dhcp_lease: PASS\r?$' "$serial" || return 1
    grep -Eq $'\\[TEST\\] network_gateway_icmp: PASS\r?$' "$serial" || return 1
    grep -Eq $'\\[TEST\\] ALL_REQUIRED_TESTS_PASSED\r?$' "$serial" || return 1
    if [[ "$require_tls" == true ]]; then
        grep -Eq $'\\[TEST\\] tls_https_optional: PASS\r?$' "$serial" || return 1
    fi
    return 0
}

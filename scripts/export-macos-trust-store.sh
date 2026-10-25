#!/usr/bin/env bash
set -euo pipefail

output="${1:-}"
if [[ -z "$output" ]]; then
    echo "usage: export-macos-trust-store.sh OUTPUT" >&2
    exit 2
fi
if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "export-macos-trust-store.sh requires macOS" >&2
    exit 1
fi
command -v security >/dev/null 2>&1 || {
    echo "macOS security tool is unavailable" >&2
    exit 1
}

readonly keychain="/System/Library/Keychains/SystemRootCertificates.keychain"
readonly maximum_bytes=$((512 * 1024 - 1))
[[ -f "$keychain" ]] || {
    echo "macOS system root keychain not found: $keychain" >&2
    exit 1
}

mkdir -p "$(dirname "$output")"
tmp="$(mktemp "${TMPDIR:-/tmp}/kurogane-trust.XXXXXX")"
cleanup() { rm -f -- "$tmp"; }
trap cleanup EXIT

security find-certificate -a -p "$keychain" > "$tmp"
certificate_count="$(grep -c '^-----BEGIN CERTIFICATE-----$' "$tmp" || true)"
bytes="$(wc -c < "$tmp" | tr -d '[:space:]')"

if [[ ! "$certificate_count" =~ ^[0-9]+$ ]] || ((certificate_count < 20)); then
    echo "macOS trust export returned too few certificates: $certificate_count" >&2
    exit 1
fi
if [[ ! "$bytes" =~ ^[0-9]+$ ]] || ((bytes == 0 || bytes > maximum_bytes)); then
    echo "macOS trust export exceeds KuroganeOS bounded trust store: ${bytes} bytes" >&2
    echo "maximum: ${maximum_bytes} bytes" >&2
    exit 1
fi

cp "$tmp" "$output"
printf '[trust] macOS SystemRootCertificates: %s roots, %s bytes -> %s\n' \
    "$certificate_count" "$bytes" "$output"

#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
header="${1:-$root/common/version.h}"

[[ $# -le 1 ]] || {
    echo "usage: bash ./scripts/read-version.sh [VERSION_HEADER]" >&2
    exit 2
}
[[ -f "$header" ]] || {
    echo "KuroganeOS version header not found: $header" >&2
    exit 1
}

# Consume all trailing whitespace so a CRLF checkout cannot leak '\r' into
# artifact names on Linux/WSL. Require one canonical, filename-safe value.
version="$(LC_ALL=C sed -n \
    's/^#define[[:space:]][[:space:]]*KUROGANE_VERSION_STRING[[:space:]][[:space:]]*"\([^"]*\)"[[:space:]]*$/\1/p' \
    "$header")"
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.+-][0-9A-Za-z]+)*$ ]]; then
    echo "invalid or ambiguous KUROGANE_VERSION_STRING in $header" >&2
    exit 1
fi

printf '%s\n' "$version"

#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
ubsan=false
if [[ "${1:-}" == --ubsan ]]; then ubsan=true; shift; fi
if (($#)); then
    echo "usage: bash scripts/test-tls-http.sh [--ubsan]" >&2
    exit 2
fi

compiler="${HOST_CXX:-c++}"
out="${HOST_TEST_DIR:-build/tests/host}"
mkdir -p "$out"
gc_flag=-Wl,--gc-sections
if [[ "$(uname -s)" == Darwin ]]; then gc_flag=-Wl,-dead_strip; fi
flags=(-std=c++17 -Wall -Wextra -Wpedantic -Werror
    -ffunction-sections -fdata-sections
    -Ikernel/net/tls -Ithird_party/mbedtls/include -Ithird_party/mbedtls/library
    '-DMBEDTLS_CONFIG_FILE="kurogane_mbedtls_config.h"')
# Use the same production-client fixture and logger in the complete host suite
# and the standalone CI gate. Neither runner links privileged kernel logging.
sources=(tests/test_tls_http_io.cpp tests/host_log_stub.cpp)
"$compiler" "${flags[@]}" -O2 "${sources[@]}" "$gc_flag" -o "$out/test_tls_http_io"
"$out/test_tls_http_io"
echo "[tls-http] optimized: PASS"
if $ubsan; then
    "$compiler" "${flags[@]}" -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
        "${sources[@]}" "$gc_flag" -o "$out/test_tls_http_io_ubsan"
    "$out/test_tls_http_io_ubsan"
    echo "[tls-http] UBSan: PASS"
fi

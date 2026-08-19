#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MBEDTLS_DIR="$ROOT_DIR/third_party/mbedtls"
OUT_DIR="${MBEDTLS_PROBE_DIR:-$ROOT_DIR/build/tests/mbedtls-freestanding}"
CC_BIN="${CC:-gcc}"

[[ -f "$MBEDTLS_DIR/include/mbedtls/build_info.h" ]] || {
    echo "[mbedtls-probe] pinned submodule missing; run git submodule update --init --recursive" >&2
    exit 1
}

grep -Fq '#define MBEDTLS_VERSION_STRING         "3.6.7"' \
    "$MBEDTLS_DIR/include/mbedtls/build_info.h" || {
    echo "[mbedtls-probe] unexpected Mbed TLS version" >&2
    exit 1
}

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

# A host gcc invocation still sees /usr/include even with -ffreestanding.
# That used to hide missing Kurogane libc headers in CI and only fail later
# under the real x86_64-elf cross-toolchain. Keep compiler intrinsic headers
# (stddef/stdint/stdbool/stdarg) but reject the host libc completely. Standard
# libc-facing headers such as string/assert/limits come from sdk/include.
COMPILER_INCLUDE_DIR="$($CC_BIN -print-file-name=include)"
[[ -d "$COMPILER_INCLUDE_DIR" ]] || {
    echo "[mbedtls-probe] cannot locate compiler intrinsic headers for $CC_BIN" >&2
    exit 1
}

CFLAGS=(
    -std=c11 -O2 -Wall -Wextra -Wpedantic -Wundef -Werror=undef
    -Werror=implicit-function-declaration
    -ffreestanding -fno-builtin -fno-stack-protector
    -m64 -mno-red-zone -mno-mmx -mno-sse -msoft-float
    -nostdinc
    -I"$ROOT_DIR/sdk/include"
    -isystem "$COMPILER_INCLUDE_DIR"
    -I"$ROOT_DIR/kernel/net/tls"
    -I"$MBEDTLS_DIR/include"
    -I"$MBEDTLS_DIR/library"
    '-DMBEDTLS_CONFIG_FILE="kurogane_mbedtls_config.h"'
)

# First compile a minimal public-header translation unit. This catches custom
# configuration selectors that Mbed TLS headers evaluate numerically (rather
# than with defined(...)) before the larger source-profile build can hide the
# issue among unrelated diagnostics.
cat > "$OUT_DIR/config_header_probe.c" <<'EOF'
#include <mbedtls/ssl.h>
#if !defined(MBEDTLS_SSL_DTLS_CONNECTION_ID_COMPAT)
#error "Kurogane Mbed TLS config did not define the DTLS CID compatibility selector"
#endif
#if MBEDTLS_SSL_DTLS_CONNECTION_ID_COMPAT != 0
#error "Kurogane Mbed TLS config selected a non-standard DTLS CID compatibility mode"
#endif
int kurogane_mbedtls_config_header_probe(void) { return 0; }
EOF

"$CC_BIN" "${CFLAGS[@]}" -c \
    "$OUT_DIR/config_header_probe.c" \
    -o "$OUT_DIR/config_header_probe.o"

echo "[mbedtls-probe] public ssl.h + custom config: PASS"

# Compile the exact modules required by the first HTTPS client profile. This is
# deliberately a compile-only probe: Kurogane platform allocation, entropy,
# wall-clock validation and TCP callbacks are linked only after their kernel
# contracts exist and are independently tested.
SOURCES=(
    aes.c
    asn1parse.c
    asn1write.c
    base64.c
    bignum.c
    bignum_core.c
    bignum_mod.c
    bignum_mod_raw.c
    block_cipher.c
    cipher.c
    cipher_wrap.c
    constant_time.c
    ctr_drbg.c
    ecdh.c
    ecdsa.c
    ecp.c
    ecp_curves.c
    entropy.c
    entropy_poll.c
    gcm.c
    md.c
    oid.c
    pem.c
    pk.c
    pk_ecc.c
    pk_wrap.c
    pkparse.c
    platform.c
    platform_util.c
    rsa.c
    rsa_alt_helpers.c
    sha256.c
    sha512.c
    x509.c
    x509_crt.c
    ssl_ciphersuites.c
    ssl_client.c
    ssl_msg.c
    ssl_tls.c
    ssl_tls12_client.c
)

for source in "${SOURCES[@]}"; do
    path="$MBEDTLS_DIR/library/$source"
    [[ -f "$path" ]] || {
        echo "[mbedtls-probe] source missing: $source" >&2
        exit 1
    }
    echo "[mbedtls-probe] CC $source"
    "$CC_BIN" "${CFLAGS[@]}" -c "$path" -o "$OUT_DIR/${source%.c}.o"
done

echo "[mbedtls-probe] freestanding TLS 1.2/X.509 source profile: PASS"

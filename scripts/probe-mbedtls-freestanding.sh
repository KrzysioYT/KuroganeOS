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

# Do not impose -Wundef on pinned third-party Mbed TLS 3.6.7 itself. Its
# config_adjust_ssl.h intentionally undefines the DTLS CID compatibility
# selector when DTLS is disabled, while ssl.h later evaluates that selector
# numerically. KuroganeOS sources still use -Wundef/-Werror=undef; the upstream
# header quirk is isolated at the integration boundary.
CFLAGS=(
    -std=c11 -O2 -Wall -Wextra -Wpedantic
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

# Verify the finalized configuration rather than fighting Mbed TLS' own
# config-adjust pass. KuroganeOS is stream TLS only, uses no DTLS CID mode,
# avoids compiler-runtime double-width division and requires SHA-384 because
# the bundled GTS Web-PKI trust anchors are signed with SHA-384.
cat > "$OUT_DIR/config_header_probe.c" <<'EOF'
#include <mbedtls/build_info.h>
#include <mbedtls/ssl.h>
#if defined(MBEDTLS_SSL_PROTO_DTLS)
#error "KuroganeOS TLS profile unexpectedly enabled DTLS"
#endif
#if defined(MBEDTLS_SSL_DTLS_CONNECTION_ID)
#error "KuroganeOS TLS profile unexpectedly enabled DTLS CID"
#endif
#if defined(MBEDTLS_SSL_DTLS_CONNECTION_ID_COMPAT)
#error "Mbed TLS finalized config should remove DTLS CID compatibility mode when DTLS is disabled"
#endif
#if !defined(MBEDTLS_NO_UDBL_DIVISION)
#error "KuroganeOS freestanding Mbed TLS profile must avoid double-width division runtime helpers"
#endif
#if defined(MBEDTLS_SHA1_C)
#error "KuroganeOS first Web-PKI profile must not link legacy SHA-1"
#endif
#if !defined(MBEDTLS_SHA384_C)
#error "KuroganeOS Web-PKI profile must enable SHA-384"
#endif
#if !defined(MBEDTLS_MD_CAN_SHA384)
#error "Mbed TLS finalized config cannot parse SHA-384 certificate signatures"
#endif
int kurogane_mbedtls_config_header_probe(void) { return 0; }
EOF

"$CC_BIN" "${CFLAGS[@]}" -c \
    "$OUT_DIR/config_header_probe.c" \
    -o "$OUT_DIR/config_header_probe.o"

echo "[mbedtls-probe] finalized stream TLS config: PASS"

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

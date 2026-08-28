#ifndef KUROGANE_MBEDTLS_CONFIG_H
#define KUROGANE_MBEDTLS_CONFIG_H

/* This configuration is intentionally client-only and freestanding. */
#define MBEDTLS_CONFIG_VERSION 0x03060700

/* Platform isolation: never silently bind to host libc, files or /dev/random. */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_STD_MEM_HDR "kurogane_mbedtls_platform.h"
#define MBEDTLS_PLATFORM_CALLOC_MACRO ku_tls_calloc
#define MBEDTLS_PLATFORM_FREE_MACRO ku_tls_free
#define MBEDTLS_PLATFORM_SNPRINTF_MACRO ku_tls_snprintf
#define MBEDTLS_NO_PLATFORM_ENTROPY

/*
 * Hosted build machines expose AF_INET/AF_INET6 through system headers, which
 * makes x509_crt.c select the platform inet_pton(). KuroganeOS deliberately
 * links the kernel without a hosted libc, so that symbol does not exist in the
 * final kernel. Force Mbed TLS' self-contained IPv4/IPv6 parser instead.
 */
#define MBEDTLS_TEST_SW_INET_PTON

/*
 * KuroganeOS links a freestanding kernel directly instead of relying on a
 * hosted compiler driver to inject compiler-runtime helpers. Prevent Mbed TLS
 * bignum code from lowering 128-bit division to helpers such as __udivti3.
 * The portable division path is slower but deterministic on every supported
 * build host and does not change cryptographic results.
 */
#define MBEDTLS_NO_UDBL_DIVISION

/* TLS client protocol surface. TLS 1.3 is added after the 1.2 transport works. */
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED

/*
 * KuroganeOS performs an additional explicit validity-interval check against
 * its own RTC after Mbed TLS has verified the chain and hostname. Mbed TLS
 * 3.6.x only guarantees mbedtls_ssl_get_peer_cert() after the handshake when
 * peer-certificate retention is enabled. Without this option the handshake
 * can verify successfully but the post-handshake security check receives a
 * null peer certificate and must fail closed.
 */
#define MBEDTLS_SSL_KEEP_PEER_CERTIFICATE

/*
 * Do not define MBEDTLS_SSL_DTLS_CONNECTION_ID_COMPAT here. Mbed TLS 3.6.7
 * config_adjust_ssl.h deliberately undefines every DTLS CID selector when
 * MBEDTLS_SSL_PROTO_DTLS is disabled. Its public ssl.h later evaluates that
 * selector numerically without a defined() guard. KuroganeOS keeps DTLS
 * disabled and isolates that upstream -Wundef diagnostic at the third-party
 * include boundary rather than enabling an unused protocol feature.
 */

/* Mbed TLS debug.h includes <inttypes.h> solely to obtain PRId64 for its
 * millisecond timestamp formatter. KuroganeOS does not expose a hosted libc
 * inttypes header to the kernel build, so provide the x86-64 formatter here.
 * This does not enable the Mbed TLS debug module. */
#define MBEDTLS_PRINTF_MS_TIME "lld"

/* Certificate/key formats used by the public Web PKI. */
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_RSASSA_PSS_SUPPORT
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_SECP521R1_ENABLED
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C

/*
 * Public Web PKI roots and intermediates commonly use SHA-384 signatures.
 * In Mbed TLS 3.6.x SHA-384 is a distinct capability flag: MBEDTLS_SHA512_C
 * does not imply MBEDTLS_SHA384_C even though both algorithms share sha512.c.
 * Without this flag GTS Root R1/R4 fail during x509_crt_parse() with
 * MBEDTLS_ERR_X509_UNKNOWN_SIG_ALG + MBEDTLS_ERR_OID_NOT_FOUND (-0x262E).
 */

/* TLS 1.2 AEAD, digest algorithms and DRBG. */
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_MD_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_ENTROPY_FORCE_SHA256
#define MBEDTLS_ENTROPY_MAX_SOURCES 2

/* Bound RAM and work. 16 KiB input remains compatible with standard TLS records. */
#define MBEDTLS_SSL_IN_CONTENT_LEN 16384
#define MBEDTLS_SSL_OUT_CONTENT_LEN 4096
#define MBEDTLS_MPI_MAX_SIZE 512
#define MBEDTLS_ECP_WINDOW_SIZE 2
#define MBEDTLS_ECP_FIXED_POINT_OPTIM 0
#define MBEDTLS_AES_ROM_TABLES

/* Restrict the initial implementation to modern AEAD ciphersuites. */
#define MBEDTLS_SSL_CIPHERSUITES \
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256, \
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384, \
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256, \
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384

/* Deliberately absent: NET_C, FS_IO, server TLS, DTLS, SHA-1, debug/error text. */

#endif

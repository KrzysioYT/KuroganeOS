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

/* TLS client protocol surface. TLS 1.3 is added after the 1.2 transport works. */
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED

/* ssl.h evaluates this compatibility selector even when DTLS CID is disabled.
 * Define the standards-based value explicitly so freestanding -Wundef builds
 * remain warning-clean without enabling DTLS itself. */
#define MBEDTLS_SSL_DTLS_CONNECTION_ID_COMPAT 0

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
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C

/* TLS 1.2 AEAD and DRBG. */
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_MD_C
#define MBEDTLS_SHA256_C
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

#ifndef KUROGANE_MBEDTLS_PLATFORM_H
#define KUROGANE_MBEDTLS_PLATFORM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Platform contract used by the pinned Mbed TLS build. Implementations live in
 * the Kurogane TLS service and use bounded kernel/runtime facilities.
 * Declaring these here prevents the library from falling back to host libc.
 */
void* ku_tls_calloc(size_t count, size_t size);
void ku_tls_free(void* pointer);
int ku_tls_snprintf(char* output, size_t capacity, const char* format, ...);

/*
 * CSPRNG seed source for mbedtls_entropy_add_source(). Kurogane deliberately
 * fails closed when the CPU exposes neither RDSEED nor RDRAND; PIT/TSC timing
 * is not accepted as cryptographic entropy.
 */
int ku_tls_hardware_entropy(
    void* context,
    unsigned char* output,
    size_t length,
    size_t* output_length);

int ku_tls_hardware_entropy_available(void);

#ifdef __cplusplus
}
#endif

#endif

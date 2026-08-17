#ifndef KUROGANE_MBEDTLS_PLATFORM_H
#define KUROGANE_MBEDTLS_PLATFORM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Platform contract used by the pinned Mbed TLS build. Implementations live in
 * the Kurogane TLS service and must use bounded kernel/runtime facilities.
 * Declaring these here prevents the library from falling back to host libc.
 */
void* ku_tls_calloc(size_t count, size_t size);
void ku_tls_free(void* pointer);
int ku_tls_snprintf(char* output, size_t capacity, const char* format, ...);

#ifdef __cplusplus
}
#endif

#endif

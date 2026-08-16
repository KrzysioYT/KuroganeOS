#ifndef KUROGANE_LIBC_STDLIB_H
#define KUROGANE_LIBC_STDLIB_H
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC system_header
#endif

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include_next <stdlib.h>
#else

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void* malloc(size_t size);
void free(void* memory);
__attribute__((noreturn)) void exit(int status);

#ifdef __cplusplus
}
#endif
#endif
#endif

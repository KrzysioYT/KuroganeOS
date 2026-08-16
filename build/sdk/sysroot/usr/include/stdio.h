#ifndef KUROGANE_LIBC_STDIO_H
#define KUROGANE_LIBC_STDIO_H
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC system_header
#endif

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include_next <stdio.h>
#else

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int putchar(int character);
int puts(const char* text);
int printf(const char* format, ...);

#ifdef __cplusplus
}
#endif
#endif
#endif

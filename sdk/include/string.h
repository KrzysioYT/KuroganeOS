#ifndef KUROGANE_LIBC_STRING_H
#define KUROGANE_LIBC_STRING_H
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC system_header
#endif

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include_next <string.h>
#else

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void* memcpy(void* destination, const void* source, size_t size);
void* memmove(void* destination, const void* source, size_t size);
void* memset(void* destination, int value, size_t size);
int memcmp(const void* left, const void* right, size_t size);
size_t strlen(const char* text);
int strcmp(const char* left, const char* right);
int strncmp(const char* left, const char* right, size_t size);
char* strchr(const char* text, int character);
char* strstr(const char* haystack, const char* needle);
char* strcpy(char* destination, const char* source);
size_t strlcpy(char* destination, const char* source, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
#endif

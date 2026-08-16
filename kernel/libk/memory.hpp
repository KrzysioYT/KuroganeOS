#pragma once

#include <stddef.h>

void* k_memcpy(void* destination, const void* source, size_t count);
void* k_memmove(void* destination, const void* source, size_t count);
void* k_memset(void* destination, int value, size_t count);
int k_memcmp(const void* left, const void* right, size_t count);

#pragma once

#include <stdarg.h>
#include <stddef.h>

int k_vsnprintf(char* output, size_t capacity, const char* format, va_list arguments);
int k_snprintf(char* output, size_t capacity, const char* format, ...);

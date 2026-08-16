#pragma once

#include <stddef.h>

size_t k_strlen(const char* text);
size_t k_strnlen(const char* text, size_t maximum);
int k_strcmp(const char* left, const char* right);
int k_strncmp(const char* left, const char* right, size_t count);

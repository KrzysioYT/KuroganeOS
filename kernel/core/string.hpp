#pragma once

#include <stddef.h>
#include <stdint.h>

namespace kstd {

size_t strlen(const char* text);
size_t strnlen(const char* text, size_t maximum);
int strcmp(const char* left, const char* right);
int strncmp(const char* left, const char* right, size_t count);
bool streq(const char* left, const char* right);
size_t copy(char* destination, size_t capacity, const char* source);
size_t append(char* destination, size_t capacity, const char* source);
bool starts_with(const char* text, const char* prefix);
char ascii_to_lower(char value);
bool parse_u64(const char* text, uint64_t& value, uint32_t base = 0);
size_t format_u64(char* output, size_t capacity, uint64_t value,
                  uint32_t base = 10, bool uppercase = false);
size_t format_i64(char* output, size_t capacity, int64_t value);

} // namespace kstd

extern "C" size_t strlen(const char* text);

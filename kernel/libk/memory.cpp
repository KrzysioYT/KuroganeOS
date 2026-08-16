#include "memory.hpp"

#include <stdint.h>

void* k_memcpy(void* destination, const void* source, size_t count) {
    auto* output = static_cast<uint8_t*>(destination);
    const auto* input = static_cast<const uint8_t*>(source);
    for (size_t index = 0; index < count; ++index) {
        output[index] = input[index];
    }
    return destination;
}

void* k_memmove(void* destination, const void* source, size_t count) {
    auto* output = static_cast<uint8_t*>(destination);
    const auto* input = static_cast<const uint8_t*>(source);
    if (output < input) {
        return k_memcpy(destination, source, count);
    }
    if (output > input) {
        for (size_t index = count; index != 0; --index) {
            output[index - 1] = input[index - 1];
        }
    }
    return destination;
}

void* k_memset(void* destination, int value, size_t count) {
    auto* output = static_cast<uint8_t*>(destination);
    for (size_t index = 0; index < count; ++index) {
        output[index] = static_cast<uint8_t>(value);
    }
    return destination;
}

int k_memcmp(const void* left, const void* right, size_t count) {
    const auto* lhs = static_cast<const uint8_t*>(left);
    const auto* rhs = static_cast<const uint8_t*>(right);
    for (size_t index = 0; index < count; ++index) {
        if (lhs[index] != rhs[index]) {
            return static_cast<int>(lhs[index]) - static_cast<int>(rhs[index]);
        }
    }
    return 0;
}

#include "memory.hpp"

#include <stdint.h>

extern "C" void* memset(void* destination, int value, size_t count) {
    auto* output = static_cast<uint8_t*>(destination);
    for (size_t i = 0; i < count; ++i) {
        output[i] = static_cast<uint8_t>(value);
    }
    return destination;
}

extern "C" void* memcpy(void* destination, const void* source, size_t count) {
    auto* output = static_cast<uint8_t*>(destination);
    const auto* input = static_cast<const uint8_t*>(source);
    for (size_t i = 0; i < count; ++i) {
        output[i] = input[i];
    }
    return destination;
}

extern "C" void* memmove(void* destination, const void* source, size_t count) {
    auto* output = static_cast<uint8_t*>(destination);
    const auto* input = static_cast<const uint8_t*>(source);
    if (output < input) {
        for (size_t i = 0; i < count; ++i) {
            output[i] = input[i];
        }
    } else if (output > input) {
        for (size_t i = count; i > 0; --i) {
            output[i - 1] = input[i - 1];
        }
    }
    return destination;
}

extern "C" int memcmp(const void* left, const void* right, size_t count) {
    const auto* lhs = static_cast<const uint8_t*>(left);
    const auto* rhs = static_cast<const uint8_t*>(right);
    for (size_t i = 0; i < count; ++i) {
        if (lhs[i] != rhs[i]) {
            return static_cast<int>(lhs[i]) - static_cast<int>(rhs[i]);
        }
    }
    return 0;
}

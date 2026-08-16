#pragma once

#include <stddef.h>
#include <stdint.h>

namespace libk {

template<typename T>
constexpr T min(T left, T right) { return left < right ? left : right; }

template<typename T>
constexpr T max(T left, T right) { return left > right ? left : right; }

template<typename T>
constexpr bool is_power_of_two(T value) {
    return value != 0 && (value & (value - 1)) == 0;
}

template<typename T>
constexpr bool align_up(T value, T alignment, T* result) {
    if (result == nullptr || !is_power_of_two(alignment)) {
        return false;
    }
    const T mask = alignment - 1;
    if (value > static_cast<T>(~static_cast<T>(0)) - mask) {
        return false;
    }
    *result = (value + mask) & ~mask;
    return true;
}

} // namespace libk

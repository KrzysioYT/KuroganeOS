#include "hash.hpp"

uint64_t k_fnv1a64(const void* data, size_t size) {
    constexpr uint64_t offset_basis = UINT64_C(14695981039346656037);
    constexpr uint64_t prime = UINT64_C(1099511628211);
    if (data == nullptr && size != 0) {
        return 0;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t value = offset_basis;
    for (size_t index = 0; index < size; ++index) {
        value ^= bytes[index];
        value *= prime;
    }
    return value;
}

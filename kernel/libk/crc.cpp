#include "crc.hpp"

uint32_t k_crc32(const void* data, size_t size) {
    if (data == nullptr && size != 0) {
        return 0;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < size; ++index) {
        crc ^= bytes[index];
        for (uint32_t bit = 0; bit < 8U; ++bit) {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }
    return ~crc;
}

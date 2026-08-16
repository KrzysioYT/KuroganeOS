#include "utf8.hpp"

KStatus k_utf8_decode_one(
    const char* text,
    size_t size,
    uint32_t* codepoint,
    size_t* consumed) {
    if (text == nullptr || codepoint == nullptr || consumed == nullptr) {
        return KStatus::InvalidArgument;
    }
    *codepoint = 0;
    *consumed = 0;
    if (size == 0) {
        return KStatus::BufferTooSmall;
    }
    const auto first = static_cast<uint8_t>(text[0]);
    size_t length = 0;
    uint32_t value = 0;
    uint32_t minimum = 0;
    if (first < 0x80U) {
        length = 1;
        value = first;
    } else if ((first & 0xE0U) == 0xC0U) {
        length = 2;
        value = first & 0x1FU;
        minimum = 0x80U;
    } else if ((first & 0xF0U) == 0xE0U) {
        length = 3;
        value = first & 0x0FU;
        minimum = 0x800U;
    } else if ((first & 0xF8U) == 0xF0U) {
        length = 4;
        value = first & 0x07U;
        minimum = 0x10000U;
    } else {
        return KStatus::Corrupted;
    }
    if (size < length) {
        return KStatus::BufferTooSmall;
    }
    for (size_t index = 1; index < length; ++index) {
        const auto byte = static_cast<uint8_t>(text[index]);
        if ((byte & 0xC0U) != 0x80U) {
            return KStatus::Corrupted;
        }
        value = (value << 6U) | (byte & 0x3FU);
    }
    if ((length != 1 && value < minimum) || value > 0x10FFFFU ||
        (value >= 0xD800U && value <= 0xDFFFU)) {
        return KStatus::Corrupted;
    }
    *codepoint = value;
    *consumed = length;
    return KStatus::Ok;
}

KStatus k_utf8_validate(const char* text, size_t size) {
    if (text == nullptr && size != 0) {
        return KStatus::InvalidArgument;
    }
    size_t offset = 0;
    while (offset < size) {
        uint32_t codepoint = 0;
        size_t consumed = 0;
        const KStatus status = k_utf8_decode_one(
            text + offset, size - offset, &codepoint, &consumed);
        if (status != KStatus::Ok) {
            return status;
        }
        offset += consumed;
    }
    return KStatus::Ok;
}

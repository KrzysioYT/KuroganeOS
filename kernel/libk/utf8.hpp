#pragma once

#include <stddef.h>
#include <stdint.h>

#include "status.hpp"

KStatus k_utf8_decode_one(
    const char* text,
    size_t size,
    uint32_t* codepoint,
    size_t* consumed);
KStatus k_utf8_validate(const char* text, size_t size);

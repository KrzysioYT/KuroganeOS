#pragma once

namespace libk {
[[noreturn]] void assertion_failed(
    const char* expression,
    const char* file,
    unsigned int line);
}

#define KASSERT(expression)                                                   \
    do {                                                                      \
        if (!(expression)) {                                                  \
            ::libk::assertion_failed(#expression, __FILE__, __LINE__);        \
        }                                                                     \
    } while (false)

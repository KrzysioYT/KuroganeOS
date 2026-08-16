#include "string.hpp"

size_t k_strlen(const char* text) {
    if (text == nullptr) {
        return 0;
    }
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

size_t k_strnlen(const char* text, size_t maximum) {
    if (text == nullptr) {
        return 0;
    }
    size_t length = 0;
    while (length < maximum && text[length] != '\0') {
        ++length;
    }
    return length;
}

int k_strcmp(const char* left, const char* right) {
    if (left == right) {
        return 0;
    }
    if (left == nullptr) {
        return -1;
    }
    if (right == nullptr) {
        return 1;
    }
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return static_cast<int>(static_cast<unsigned char>(*left)) -
        static_cast<int>(static_cast<unsigned char>(*right));
}

int k_strncmp(const char* left, const char* right, size_t count) {
    if (left == right || count == 0) {
        return 0;
    }
    if (left == nullptr) {
        return -1;
    }
    if (right == nullptr) {
        return 1;
    }
    for (size_t index = 0; index < count; ++index) {
        const auto lhs = static_cast<unsigned char>(left[index]);
        const auto rhs = static_cast<unsigned char>(right[index]);
        if (lhs != rhs || lhs == 0 || rhs == 0) {
            return static_cast<int>(lhs) - static_cast<int>(rhs);
        }
    }
    return 0;
}

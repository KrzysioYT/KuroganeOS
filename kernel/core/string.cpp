#include "string.hpp"

namespace kstd {

size_t strlen(const char* text) {
    if (!text) {
        return 0;
    }
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

size_t strnlen(const char* text, size_t maximum) {
    if (!text) {
        return 0;
    }
    size_t length = 0;
    while (length < maximum && text[length] != '\0') {
        ++length;
    }
    return length;
}

int strcmp(const char* left, const char* right) {
    if (left == right) {
        return 0;
    }
    if (!left) {
        return -1;
    }
    if (!right) {
        return 1;
    }
    while (*left && *right && *left == *right) {
        ++left;
        ++right;
    }
    return static_cast<unsigned char>(*left) -
           static_cast<unsigned char>(*right);
}

int strncmp(const char* left, const char* right, size_t count) {
    if (left == right || count == 0) {
        return 0;
    }
    if (!left) {
        return -1;
    }
    if (!right) {
        return 1;
    }
    for (size_t i = 0; i < count; ++i) {
        const auto lhs = static_cast<unsigned char>(left[i]);
        const auto rhs = static_cast<unsigned char>(right[i]);
        if (lhs != rhs || lhs == '\0' || rhs == '\0') {
            return static_cast<int>(lhs) - static_cast<int>(rhs);
        }
    }
    return 0;
}

bool streq(const char* left, const char* right) {
    return strcmp(left, right) == 0;
}

size_t copy(char* destination, size_t capacity, const char* source) {
    if (!destination || capacity == 0) {
        return 0;
    }
    size_t written = 0;
    if (source) {
        while (written + 1 < capacity && source[written] != '\0') {
            destination[written] = source[written];
            ++written;
        }
    }
    destination[written] = '\0';
    return written;
}

size_t append(char* destination, size_t capacity, const char* source) {
    if (!destination || capacity == 0) {
        return 0;
    }
    size_t length = strnlen(destination, capacity);
    if (length == capacity) {
        destination[capacity - 1] = '\0';
        return capacity - 1;
    }
    return length + copy(destination + length, capacity - length, source);
}

bool starts_with(const char* text, const char* prefix) {
    if (!text || !prefix) {
        return false;
    }
    while (*prefix) {
        if (*text++ != *prefix++) {
            return false;
        }
    }
    return true;
}

char ascii_to_lower(char value) {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value + ('a' - 'A'));
    }
    return value;
}

bool parse_u64(const char* text, uint64_t& value, uint32_t base) {
    if (!text) {
        return false;
    }
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    if (base == 0) {
        if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
            base = 16;
            text += 2;
        } else {
            base = 10;
        }
    }
    if (base < 2 || base > 16 || *text == '\0') {
        return false;
    }

    uint64_t result = 0;
    bool any = false;
    while (*text) {
        uint32_t digit;
        if (*text >= '0' && *text <= '9') {
            digit = static_cast<uint32_t>(*text - '0');
        } else if (*text >= 'a' && *text <= 'f') {
            digit = static_cast<uint32_t>(*text - 'a' + 10);
        } else if (*text >= 'A' && *text <= 'F') {
            digit = static_cast<uint32_t>(*text - 'A' + 10);
        } else {
            break;
        }
        if (digit >= base || result > (UINT64_MAX - digit) / base) {
            return false;
        }
        result = result * base + digit;
        any = true;
        ++text;
    }
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    if (*text != '\0' || !any) {
        return false;
    }
    value = result;
    return true;
}

size_t format_u64(char* output, size_t capacity, uint64_t value,
                  uint32_t base, bool uppercase) {
    if (!output || capacity == 0 || base < 2 || base > 16) {
        return 0;
    }
    const char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char reverse[65];
    size_t count = 0;
    do {
        reverse[count++] = digits[value % base];
        value /= base;
    } while (value != 0 && count < sizeof(reverse));

    const size_t written = count < capacity - 1 ? count : capacity - 1;
    for (size_t i = 0; i < written; ++i) {
        output[i] = reverse[count - i - 1];
    }
    output[written] = '\0';
    return written;
}

size_t format_i64(char* output, size_t capacity, int64_t value) {
    if (!output || capacity == 0) {
        return 0;
    }
    if (value >= 0) {
        return format_u64(output, capacity, static_cast<uint64_t>(value));
    }
    if (capacity < 2) {
        output[0] = '\0';
        return 0;
    }
    output[0] = '-';
    const uint64_t magnitude = static_cast<uint64_t>(-(value + 1)) + 1;
    return 1 + format_u64(output + 1, capacity - 1, magnitude);
}

} // namespace kstd

extern "C" size_t strlen(const char* text) {
    return kstd::strlen(text);
}

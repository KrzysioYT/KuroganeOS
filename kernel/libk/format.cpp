#include "format.hpp"

#include <stdint.h>

#include "string.hpp"

namespace {

struct Writer {
    char* output;
    size_t capacity;
    size_t required;

    void put(char value) {
        if (output != nullptr && capacity != 0 && required + 1 < capacity) {
            output[required] = value;
        }
        ++required;
    }

    void finish() {
        if (output == nullptr || capacity == 0) {
            return;
        }
        const size_t end = required < capacity ? required : capacity - 1;
        output[end] = '\0';
    }
};

void write_unsigned(
    Writer& writer,
    uint64_t value,
    uint32_t base,
    bool uppercase,
    size_t width,
    char padding) {
    const char* digits = uppercase
        ? "0123456789ABCDEF"
        : "0123456789abcdef";
    char reverse[64];
    size_t count = 0;
    do {
        reverse[count++] = digits[value % base];
        value /= base;
    } while (value != 0);
    while (count < width) {
        writer.put(padding);
        --width;
    }
    while (count != 0) {
        writer.put(reverse[--count]);
    }
}

void write_signed(Writer& writer, int64_t value, size_t width, char padding) {
    const bool negative = value < 0;
    const uint64_t magnitude = negative
        ? static_cast<uint64_t>(-(value + 1)) + 1U
        : static_cast<uint64_t>(value);
    if (negative && padding == '0') {
        writer.put('-');
        if (width != 0) {
            --width;
        }
        write_unsigned(writer, magnitude, 10, false, width, padding);
        return;
    }
    char reverse[64];
    size_t count = 0;
    uint64_t remaining = magnitude;
    do {
        reverse[count++] = static_cast<char>('0' + remaining % 10U);
        remaining /= 10U;
    } while (remaining != 0);
    const size_t required = count + (negative ? 1U : 0U);
    for (size_t index = required; index < width; ++index) {
        writer.put(padding);
    }
    if (negative) {
        writer.put('-');
    }
    while (count != 0) {
        writer.put(reverse[--count]);
    }
}

} // namespace

int k_vsnprintf(char* output, size_t capacity, const char* format, va_list arguments) {
    if (format == nullptr || (output == nullptr && capacity != 0)) {
        return -1;
    }
    Writer writer{output, capacity, 0};
    for (size_t index = 0; format[index] != '\0'; ++index) {
        if (format[index] != '%') {
            writer.put(format[index]);
            continue;
        }
        ++index;
        if (format[index] == '%') {
            writer.put('%');
            continue;
        }

        char padding = ' ';
        if (format[index] == '0') {
            padding = '0';
            ++index;
        }
        size_t width = 0;
        while (format[index] >= '0' && format[index] <= '9') {
            const size_t digit = static_cast<size_t>(format[index] - '0');
            if (width <= (SIZE_MAX - digit) / 10U) {
                width = width * 10U + digit;
            }
            ++index;
        }

        enum class Length { Normal, Long, LongLong, Size };
        Length length = Length::Normal;
        if (format[index] == 'z') {
            length = Length::Size;
            ++index;
        } else if (format[index] == 'l') {
            length = Length::Long;
            ++index;
            if (format[index] == 'l') {
                length = Length::LongLong;
                ++index;
            }
        }

        const char conversion = format[index];
        if (conversion == 's') {
            const char* text = va_arg(arguments, const char*);
            if (text == nullptr) {
                text = "(null)";
            }
            const size_t length_value = k_strlen(text);
            for (size_t pad = length_value; pad < width; ++pad) {
                writer.put(' ');
            }
            for (size_t offset = 0; offset < length_value; ++offset) {
                writer.put(text[offset]);
            }
        } else if (conversion == 'c') {
            writer.put(static_cast<char>(va_arg(arguments, int)));
        } else if (conversion == 'd' || conversion == 'i') {
            int64_t value = 0;
            switch (length) {
                case Length::Normal: value = va_arg(arguments, int); break;
                case Length::Long: value = va_arg(arguments, long); break;
                case Length::LongLong: value = va_arg(arguments, long long); break;
                case Length::Size:
                    value = static_cast<int64_t>(va_arg(arguments, ptrdiff_t));
                    break;
            }
            write_signed(writer, value, width, padding);
        } else if (conversion == 'u' || conversion == 'x' || conversion == 'X') {
            uint64_t value = 0;
            switch (length) {
                case Length::Normal: value = va_arg(arguments, unsigned int); break;
                case Length::Long: value = va_arg(arguments, unsigned long); break;
                case Length::LongLong:
                    value = va_arg(arguments, unsigned long long);
                    break;
                case Length::Size: value = va_arg(arguments, size_t); break;
            }
            write_unsigned(
                writer,
                value,
                conversion == 'u' ? 10U : 16U,
                conversion == 'X',
                width,
                padding);
        } else if (conversion == 'p') {
            writer.put('0');
            writer.put('x');
            write_unsigned(
                writer,
                reinterpret_cast<uintptr_t>(va_arg(arguments, void*)),
                16U,
                false,
                sizeof(uintptr_t) * 2U,
                '0');
        } else {
            writer.put('%');
            if (conversion == '\0') {
                break;
            }
            writer.put(conversion);
        }
    }
    writer.finish();
    return writer.required <= static_cast<size_t>(INT32_MAX)
        ? static_cast<int>(writer.required)
        : -1;
}

int k_snprintf(char* output, size_t capacity, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int result = k_vsnprintf(output, capacity, format, arguments);
    va_end(arguments);
    return result;
}

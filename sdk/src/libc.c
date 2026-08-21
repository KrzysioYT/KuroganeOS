#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <kurogane/status.h>
#include <kurogane/syscall.h>

void* memcpy(void* destination, const void* source, size_t size) {
    unsigned char* output = (unsigned char*)destination;
    const unsigned char* input = (const unsigned char*)source;
    for (size_t index = 0; index < size; ++index) output[index] = input[index];
    return destination;
}

void* memmove(void* destination, const void* source, size_t size) {
    unsigned char* output = (unsigned char*)destination;
    const unsigned char* input = (const unsigned char*)source;
    if (output < input) {
        for (size_t index = 0; index < size; ++index) output[index] = input[index];
    } else if (output > input) {
        for (size_t index = size; index != 0; --index) output[index - 1] = input[index - 1];
    }
    return destination;
}

void* memset(void* destination, int value, size_t size) {
    unsigned char* output = (unsigned char*)destination;
    for (size_t index = 0; index < size; ++index) output[index] = (unsigned char)value;
    return destination;
}

int memcmp(const void* left, const void* right, size_t size) {
    const unsigned char* a = (const unsigned char*)left;
    const unsigned char* b = (const unsigned char*)right;
    for (size_t index = 0; index < size; ++index) {
        if (a[index] != b[index]) return a[index] < b[index] ? -1 : 1;
    }
    return 0;
}

size_t strlen(const char* text) {
    size_t length = 0;
    if (text != (const char*)0) while (text[length] != '\0') ++length;
    return length;
}

int strcmp(const char* left, const char* right) {
    size_t index = 0;
    while (left[index] != '\0' && left[index] == right[index]) ++index;
    return (unsigned char)left[index] - (unsigned char)right[index];
}

int strncmp(const char* left, const char* right, size_t size) {
    for (size_t index = 0; index < size; ++index) {
        const unsigned char a = (unsigned char)left[index];
        const unsigned char b = (unsigned char)right[index];
        if (a != b) return (int)a - (int)b;
        if (a == 0) return 0;
    }
    return 0;
}

char* strchr(const char* text, int character) {
    const char target = (char)character;
    if (text == (const char*)0) return (char*)0;
    for (;;) {
        if (*text == target) return (char*)text;
        if (*text == '\0') return (char*)0;
        ++text;
    }
}

char* strstr(const char* haystack, const char* needle) {
    if (haystack == (const char*)0 || needle == (const char*)0) return (char*)0;
    if (*needle == '\0') return (char*)haystack;
    for (const char* start = haystack; *start != '\0'; ++start) {
        const char* left = start;
        const char* right = needle;
        while (*right != '\0' && *left == *right) {
            ++left;
            ++right;
        }
        if (*right == '\0') return (char*)start;
    }
    return (char*)0;
}

char* strcpy(char* destination, const char* source) {
    size_t index = 0;
    do { destination[index] = source[index]; } while (source[index++] != '\0');
    return destination;
}

size_t strlcpy(char* destination, const char* source, size_t capacity) {
    const size_t length = strlen(source);
    if (capacity != 0) {
        const size_t copied = length < capacity - 1 ? length : capacity - 1;
        memcpy(destination, source, copied);
        destination[copied] = '\0';
    }
    return length;
}

void* malloc(size_t size) { return ku_alloc(size); }
void free(void* memory) { if (memory != (void*)0) (void)ku_free(memory); }
__attribute__((noreturn)) void exit(int status) { ku_exit((int32_t)status); }

ssize_t read(kuro_fd_t descriptor, void* buffer, size_t size) {
    return (ssize_t)ku_read((ku_handle_t)descriptor, buffer, size);
}
ssize_t write(kuro_fd_t descriptor, const void* buffer, size_t size) {
    return (ssize_t)ku_write((uint64_t)descriptor, buffer, size);
}
int close(kuro_fd_t descriptor) {
    return (int)ku_close((ku_handle_t)descriptor);
}
kuro_fd_t open(const char* path, int flags) {
    if (path == (const char*)0 || flags != O_RDONLY) return KU_STATUS_INVALID_ARGUMENT;
    return (kuro_fd_t)ku_open(path, strlen(path), KU_OPEN_READ);
}

static int write_all(const char* data, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        const ssize_t result = write(1, data + offset, size - offset);
        if (result <= 0) return -1;
        offset += (size_t)result;
    }
    return (int)size;
}

int putchar(int character) {
    const char byte = (char)character;
    return write_all(&byte, 1) == 1 ? (unsigned char)byte : -1;
}

int puts(const char* text) {
    const size_t size = strlen(text);
    if (write_all(text, size) < 0 || putchar('\n') < 0) return -1;
    return (int)(size + 1);
}

static int print_unsigned(uint64_t value, unsigned base, int prefix) {
    char buffer[32];
    size_t cursor = sizeof(buffer);
    static const char digits[] = "0123456789abcdef";
    do {
        buffer[--cursor] = digits[value % base];
        value /= base;
    } while (value != 0 && cursor != 0);
    int total = 0;
    if (prefix && write_all("0x", 2) < 0) return -1;
    if (prefix) total += 2;
    const int written = write_all(buffer + cursor, sizeof(buffer) - cursor);
    return written < 0 ? -1 : total + written;
}

int printf(const char* format, ...) {
    if (format == (const char*)0) return -1;
    va_list arguments;
    va_start(arguments, format);
    int total = 0;
    for (size_t index = 0; format[index] != '\0'; ++index) {
        if (format[index] != '%') {
            if (putchar(format[index]) < 0) { total = -1; break; }
            ++total;
            continue;
        }
        const char specifier = format[++index];
        int written = 0;
        if (specifier == '%') written = putchar('%') < 0 ? -1 : 1;
        else if (specifier == 'c') written = putchar(va_arg(arguments, int)) < 0 ? -1 : 1;
        else if (specifier == 's') {
            const char* text = va_arg(arguments, const char*);
            if (text == (const char*)0) text = "(null)";
            written = write_all(text, strlen(text));
        } else if (specifier == 'd') {
            const int value = va_arg(arguments, int);
            if (value < 0) {
                if (putchar('-') < 0) written = -1;
                else {
                    written = print_unsigned((uint64_t)(-(int64_t)value), 10, 0);
                    if (written >= 0) ++written;
                }
            } else written = print_unsigned((uint64_t)value, 10, 0);
        } else if (specifier == 'u') written = print_unsigned(va_arg(arguments, unsigned), 10, 0);
        else if (specifier == 'x') written = print_unsigned(va_arg(arguments, unsigned), 16, 0);
        else if (specifier == 'p') written = print_unsigned((uintptr_t)va_arg(arguments, void*), 16, 1);
        else { written = -1; }
        if (written < 0) { total = -1; break; }
        total += written;
    }
    va_end(arguments);
    return total;
}

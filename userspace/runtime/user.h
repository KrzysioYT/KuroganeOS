#ifndef KUROGANE_USER_RUNTIME_H
#define KUROGANE_USER_RUNTIME_H

#include <kurogane/status.h>
#include <kurogane/syscall.h>
#include <kurogane/types.h>

static inline size_t u_strlen(const char* text) {
    size_t length = 0U;
    if (text == (const char*)0) return 0U;
    while (text[length] != '\0') ++length;
    return length;
}

static inline int u_streq(const char* left, const char* right) {
    size_t index = 0U;
    if (left == (const char*)0 || right == (const char*)0) return 0;
    while (left[index] != '\0' && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

static inline int u_starts_with(const char* text, const char* prefix) {
    size_t index = 0U;
    if (text == (const char*)0 || prefix == (const char*)0) return 0;
    while (prefix[index] != '\0') {
        if (text[index] != prefix[index]) return 0;
        ++index;
    }
    return 1;
}

static inline int u_write_all(const char* text, size_t size) {
    size_t offset = 0U;
    while (offset < size) {
        const ku_result_t result = ku_write(1U, text + offset, size - offset);
        if (result <= 0) return 0;
        offset += (size_t)result;
    }
    return 1;
}

static inline int u_puts(const char* text) {
    return u_write_all(text, u_strlen(text));
}

static inline int u_put_u64(uint64_t value) {
    char buffer[21];
    size_t cursor = sizeof(buffer);
    if (value == 0U) return u_write_all("0", 1U);
    while (value != 0U && cursor != 0U) {
        const uint64_t digit = value % UINT64_C(10);
        buffer[--cursor] = (char)('0' + digit);
        value /= UINT64_C(10);
    }
    return u_write_all(buffer + cursor, sizeof(buffer) - cursor);
}

static inline ku_result_t u_spawn(const char* path) {
    return ku_spawn(path, u_strlen(path));
}

static inline int u_wait(uint64_t pid, int32_t* exit_code) {
    for (;;) {
        const ku_status_t status = ku_wait(pid, exit_code);
        if (status == KU_STATUS_OK) return 1;
        if (status != KU_STATUS_WOULD_BLOCK) return 0;
        (void)ku_yield();
    }
}

static inline int u_spawn_wait(const char* path, int32_t* exit_code) {
    const ku_result_t pid = u_spawn(path);
    if (pid <= 0) return 0;
    return u_wait((uint64_t)pid, exit_code);
}

#endif

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "dev_credential.h"

#define KU_DEV_PROFILE_USERNAME_CAPACITY 24U
#define KU_DEV_PROFILE_LOCALE_CAPACITY 16U

struct ku_dev_profile_data {
    char username[KU_DEV_PROFILE_USERNAME_CAPACITY];
    uint64_t password_hash;
    int password_required;
};

static inline size_t ku_dev_profile_strlen(const char* value) {
    size_t length = 0U;
    if (value == 0) return 0U;
    while (value[length] != '\0') ++length;
    return length;
}

static inline int ku_dev_profile_text_equal(
    const char* left,
    const char* right) {
    size_t index = 0U;
    if (left == 0 || right == 0) return 0;
    while (left[index] == right[index] && left[index] != '\0') ++index;
    return left[index] == right[index];
}

static inline int ku_dev_profile_copy_value(
    const char* config,
    const char* key,
    char* output,
    size_t capacity) {
    size_t key_length;
    size_t start;
    if (config == 0 || key == 0 || output == 0 || capacity == 0U) return 0;
    key_length = ku_dev_profile_strlen(key);
    for (start = 0U; config[start] != '\0'; ++start) {
        size_t index = 0U;
        size_t source;
        size_t written = 0U;
        if (start != 0U && config[start - 1U] != '\n') continue;
        while (index < key_length && config[start + index] == key[index]) ++index;
        if (index != key_length || config[start + index] != '=') continue;
        source = start + key_length + 1U;
        while (config[source] != '\0' && config[source] != '\n' &&
               written + 1U < capacity) {
            output[written++] = config[source++];
        }
        if (config[source] != '\0' && config[source] != '\n') {
            output[0] = '\0';
            return 0;
        }
        output[written] = '\0';
        return 1;
    }
    output[0] = '\0';
    return 0;
}

static inline int ku_dev_profile_valid_username(const char* value) {
    size_t index;
    const size_t length = ku_dev_profile_strlen(value);
    if (length == 0U || length >= KU_DEV_PROFILE_USERNAME_CAPACITY) return 0;
    for (index = 0U; index < length; ++index) {
        const char ch = value[index];
        const int alpha = (ch >= 'A' && ch <= 'Z') ||
                          (ch >= 'a' && ch <= 'z');
        const int digit = ch >= '0' && ch <= '9';
        if (!alpha && !digit && ch != '_' && ch != '-') return 0;
    }
    return 1;
}

static inline int ku_dev_profile_parse_hex64(
    const char* value,
    uint64_t* output) {
    uint64_t result = 0U;
    size_t index;
    if (value == 0 || output == 0 || ku_dev_profile_strlen(value) != 16U) return 0;
    for (index = 0U; index < 16U; ++index) {
        uint64_t digit;
        const char ch = value[index];
        if (ch >= '0' && ch <= '9') digit = (uint64_t)(ch - '0');
        else if (ch >= 'A' && ch <= 'F') digit = (uint64_t)(ch - 'A' + 10);
        else if (ch >= 'a' && ch <= 'f') digit = (uint64_t)(ch - 'a' + 10);
        else return 0;
        result = (result << 4U) | digit;
    }
    *output = result;
    return 1;
}

static inline int ku_dev_profile_parse_user_config(
    const char* config,
    struct ku_dev_profile_data* output) {
    char username[KU_DEV_PROFILE_USERNAME_CAPACITY];
    char required[4];
    char hash_text[24];
    char scheme[24];
    uint64_t password_hash = 0U;
    int password_required;
    size_t index;

    if (config == 0 || output == 0) return 0;
    if (!ku_dev_profile_copy_value(
            config, "USERNAME", username, sizeof(username)) ||
        !ku_dev_profile_valid_username(username) ||
        !ku_dev_profile_copy_value(
            config, "PASSWORD_REQUIRED", required, sizeof(required)) ||
        !ku_dev_profile_copy_value(
            config, "PASSWORD_HASH", hash_text, sizeof(hash_text)) ||
        !ku_dev_profile_parse_hex64(hash_text, &password_hash) ||
        !ku_dev_profile_copy_value(
            config, "HASH_SCHEME", scheme, sizeof(scheme)) ||
        !ku_dev_profile_text_equal(scheme, KU_DEV_CREDENTIAL_SCHEME)) {
        return 0;
    }

    if (ku_dev_profile_text_equal(required, "0")) password_required = 0;
    else if (ku_dev_profile_text_equal(required, "1")) password_required = 1;
    else return 0;

    /* A no-password installer profile must carry the canonical zero hash. */
    if (!password_required && password_hash != 0U) return 0;

    for (index = 0U; index < sizeof(output->username); ++index) {
        output->username[index] = username[index];
        if (username[index] == '\0') break;
    }
    output->username[sizeof(output->username) - 1U] = '\0';
    output->password_hash = password_hash;
    output->password_required = password_required;
    return 1;
}

static inline int ku_dev_profile_parse_locale(
    const char* config,
    char output[KU_DEV_PROFILE_LOCALE_CAPACITY]) {
    char locale[KU_DEV_PROFILE_LOCALE_CAPACITY];
    size_t index;
    if (config == 0 || output == 0 ||
        !ku_dev_profile_copy_value(config, "LANG", locale, sizeof(locale))) {
        return 0;
    }
    if (!ku_dev_profile_text_equal(locale, "en-US") &&
        !ku_dev_profile_text_equal(locale, "pl-PL")) {
        return 0;
    }
    for (index = 0U; index < KU_DEV_PROFILE_LOCALE_CAPACITY; ++index) {
        output[index] = locale[index];
        if (locale[index] == '\0') break;
    }
    output[KU_DEV_PROFILE_LOCALE_CAPACITY - 1U] = '\0';
    return 1;
}

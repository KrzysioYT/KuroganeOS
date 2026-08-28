#pragma once

#include <stdint.h>

/*
 * Development-only credential verifier shared by the 3.x installer/login path.
 *
 * SECURITY: FNV-1a is NOT a password KDF and must never be promoted as one.
 * The format is kept only for 3.x compatibility until the Iron Shield secure
 * credential migration replaces it with a cryptographic password KDF.
 */
#define KU_DEV_CREDENTIAL_SCHEME "FNV1A64-DEV"

static inline uint64_t ku_dev_credential_hash(
    const char* username,
    const char* password) {
    uint64_t hash = UINT64_C(1469598103934665603);
    static const char domain[] = "KuroganeOS-3.3-dev:";
    const char* fields[3];
    unsigned field;
    unsigned index;

    for (index = 0U; domain[index] != '\0'; ++index) {
        hash ^= (uint8_t)domain[index];
        hash *= UINT64_C(1099511628211);
    }

    fields[0] = username;
    fields[1] = ":";
    fields[2] = password;
    for (field = 0U; field < 3U; ++field) {
        const char* value = fields[field];
        if (value == 0) continue;
        for (index = 0U; value[index] != '\0'; ++index) {
            hash ^= (uint8_t)value[index];
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

static inline int ku_dev_credential_verify(
    const char* username,
    const char* password,
    uint64_t expected_hash) {
    return ku_dev_credential_hash(username, password) == expected_hash;
}

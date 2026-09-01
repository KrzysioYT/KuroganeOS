#ifndef KUROGANE_ANVIL_SHA256_H
#define KUROGANE_ANVIL_SHA256_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct anvil_sha256_context {
    uint32_t state[8];
    uint64_t total_bytes;
    uint8_t block[64];
    size_t block_size;
} anvil_sha256_context;

static inline uint32_t anvil_sha256_rotr(uint32_t value, unsigned count) {
    return (value >> count) | (value << (32U - count));
}

static inline uint32_t anvil_sha256_load_be32(const uint8_t* input) {
    return ((uint32_t)input[0] << 24U) |
           ((uint32_t)input[1] << 16U) |
           ((uint32_t)input[2] << 8U) |
           (uint32_t)input[3];
}

static inline void anvil_sha256_store_be32(uint8_t* output, uint32_t value) {
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)(value >> 16U);
    output[2] = (uint8_t)(value >> 8U);
    output[3] = (uint8_t)value;
}

static inline void anvil_sha256_transform(
    anvil_sha256_context* context,
    const uint8_t block[64]) {
    static const uint32_t k[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
    };
    uint32_t w[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    size_t index;

    for (index = 0U; index < 16U; ++index) {
        w[index] = anvil_sha256_load_be32(block + index * 4U);
    }
    for (index = 16U; index < 64U; ++index) {
        const uint32_t s0 =
            anvil_sha256_rotr(w[index - 15U], 7U) ^
            anvil_sha256_rotr(w[index - 15U], 18U) ^
            (w[index - 15U] >> 3U);
        const uint32_t s1 =
            anvil_sha256_rotr(w[index - 2U], 17U) ^
            anvil_sha256_rotr(w[index - 2U], 19U) ^
            (w[index - 2U] >> 10U);
        w[index] = w[index - 16U] + s0 + w[index - 7U] + s1;
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];

    for (index = 0U; index < 64U; ++index) {
        const uint32_t s1 =
            anvil_sha256_rotr(e, 6U) ^
            anvil_sha256_rotr(e, 11U) ^
            anvil_sha256_rotr(e, 25U);
        const uint32_t choose = (e & f) ^ ((~e) & g);
        const uint32_t temp1 = h + s1 + choose + k[index] + w[index];
        const uint32_t s0 =
            anvil_sha256_rotr(a, 2U) ^
            anvil_sha256_rotr(a, 13U) ^
            anvil_sha256_rotr(a, 22U);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

static inline void anvil_sha256_init(anvil_sha256_context* context) {
    context->state[0] = 0x6a09e667U;
    context->state[1] = 0xbb67ae85U;
    context->state[2] = 0x3c6ef372U;
    context->state[3] = 0xa54ff53aU;
    context->state[4] = 0x510e527fU;
    context->state[5] = 0x9b05688cU;
    context->state[6] = 0x1f83d9abU;
    context->state[7] = 0x5be0cd19U;
    context->total_bytes = 0U;
    context->block_size = 0U;
}

static inline void anvil_sha256_update(
    anvil_sha256_context* context,
    const uint8_t* data,
    size_t size) {
    size_t offset = 0U;
    if (context == NULL || (data == NULL && size != 0U)) return;
    context->total_bytes += (uint64_t)size;
    while (offset < size) {
        size_t copied = 64U - context->block_size;
        if (copied > size - offset) copied = size - offset;
        memcpy(context->block + context->block_size, data + offset, copied);
        context->block_size += copied;
        offset += copied;
        if (context->block_size == 64U) {
            anvil_sha256_transform(context, context->block);
            context->block_size = 0U;
        }
    }
}

static inline void anvil_sha256_final(
    anvil_sha256_context* context,
    uint8_t digest[32]) {
    const uint64_t bit_count = context->total_bytes * UINT64_C(8);
    size_t index;

    context->block[context->block_size++] = 0x80U;
    if (context->block_size > 56U) {
        while (context->block_size < 64U) context->block[context->block_size++] = 0U;
        anvil_sha256_transform(context, context->block);
        context->block_size = 0U;
    }
    while (context->block_size < 56U) context->block[context->block_size++] = 0U;
    for (index = 0U; index < 8U; ++index) {
        context->block[56U + index] =
            (uint8_t)(bit_count >> (56U - (unsigned)index * 8U));
    }
    anvil_sha256_transform(context, context->block);
    for (index = 0U; index < 8U; ++index) {
        anvil_sha256_store_be32(digest + index * 4U, context->state[index]);
    }
}

static inline void anvil_sha256_hex(
    const uint8_t* data,
    size_t size,
    char output[65]) {
    static const char digits[] = "0123456789abcdef";
    anvil_sha256_context context;
    uint8_t digest[32];
    size_t index;
    anvil_sha256_init(&context);
    anvil_sha256_update(&context, data, size);
    anvil_sha256_final(&context, digest);
    for (index = 0U; index < sizeof(digest); ++index) {
        output[index * 2U] = digits[digest[index] >> 4U];
        output[index * 2U + 1U] = digits[digest[index] & 0x0fU];
    }
    output[64] = '\0';
}

static inline int anvil_sha256_matches(
    const uint8_t* data,
    size_t size,
    const char* expected_hex) {
    char actual[65];
    size_t index;
    if (expected_hex == NULL) return 0;
    for (index = 0U; index < 64U; ++index) {
        const char character = expected_hex[index];
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f'))) return 0;
    }
    if (expected_hex[64] != '\0') return 0;
    anvil_sha256_hex(data, size, actual);
    return strcmp(actual, expected_hex) == 0;
}

#endif

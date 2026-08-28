#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "../userspace/gui/anvil/sha256.h"

static void expect_digest(const char* text, const char* expected) {
    char actual[65];
    anvil_sha256_hex(
        reinterpret_cast<const uint8_t*>(text),
        std::strlen(text),
        actual);
    assert(std::strcmp(actual, expected) == 0);
    assert(anvil_sha256_matches(
        reinterpret_cast<const uint8_t*>(text),
        std::strlen(text),
        expected));
}

int main() {
    expect_digest(
        "",
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    expect_digest(
        "abc",
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    expect_digest(
        "The quick brown fox jumps over the lazy dog",
        "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");

    const char payload[] = "tampered";
    assert(!anvil_sha256_matches(
        reinterpret_cast<const uint8_t*>(payload),
        sizeof(payload) - 1U,
        "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592"));
    assert(!anvil_sha256_matches(
        reinterpret_cast<const uint8_t*>(payload),
        sizeof(payload) - 1U,
        "D7A8FBB307D7809469CA9ABCB0082E4F8D5651E46D3CDB762D02D0BF37C9E592"));

    std::cout << "[TEST] anvil_sha256_vectors: PASS\n";
    return 0;
}

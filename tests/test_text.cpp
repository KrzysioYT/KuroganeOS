#include <kurogane/text.h>

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace {

void expect_codepoint(
    const char* bytes,
    size_t size,
    uint32_t expected,
    size_t expected_consumed) {
    uint32_t codepoint = 0U;
    size_t consumed = 0U;
    assert(ku_utf8_decode_one(bytes, size, &codepoint, &consumed));
    assert(codepoint == expected);
    assert(consumed == expected_consumed);
}

void expect_invalid(const char* bytes, size_t size) {
    uint32_t codepoint = 0U;
    size_t consumed = 0U;
    assert(!ku_utf8_decode_one(bytes, size, &codepoint, &consumed));
}

} // namespace

int main() {
    const ku_text_style system_style =
        ku_text_default_style(KU_TEXT_CONTEXT_SYSTEM_UI);
    const ku_text_style document_style =
        ku_text_default_style(KU_TEXT_CONTEXT_DOCUMENT);

    assert(ku_text_style_valid(&system_style));
    assert(ku_text_style_valid(&document_style));
    assert(system_style.family == KU_FONT_FAMILY_SYSTEM_UI);
    assert(document_style.family == KU_FONT_FAMILY_SANS_SERIF);
    assert(system_style.family != document_style.family);

    expect_codepoint("A", 1U, UINT32_C(0x41), 1U);

    const char a_ogonek[] = {
        static_cast<char>(0xC4), static_cast<char>(0x85)
    };
    expect_codepoint(a_ogonek, sizeof(a_ogonek), UINT32_C(0x0105), 2U);

    const char z_dot[] = {
        static_cast<char>(0xC5), static_cast<char>(0xBC)
    };
    expect_codepoint(z_dot, sizeof(z_dot), UINT32_C(0x017C), 2U);

    const char euro[] = {
        static_cast<char>(0xE2), static_cast<char>(0x82),
        static_cast<char>(0xAC)
    };
    expect_codepoint(euro, sizeof(euro), UINT32_C(0x20AC), 3U);

    const char emoji[] = {
        static_cast<char>(0xF0), static_cast<char>(0x9F),
        static_cast<char>(0x98), static_cast<char>(0x80)
    };
    expect_codepoint(emoji, sizeof(emoji), UINT32_C(0x1F600), 4U);

    const char overlong[] = {
        static_cast<char>(0xC0), static_cast<char>(0xAF)
    };
    expect_invalid(overlong, sizeof(overlong));

    const char surrogate[] = {
        static_cast<char>(0xED), static_cast<char>(0xA0),
        static_cast<char>(0x80)
    };
    expect_invalid(surrogate, sizeof(surrogate));

    const char too_large[] = {
        static_cast<char>(0xF4), static_cast<char>(0x90),
        static_cast<char>(0x80), static_cast<char>(0x80)
    };
    expect_invalid(too_large, sizeof(too_large));

    const char truncated[] = {
        static_cast<char>(0xE2), static_cast<char>(0x82)
    };
    expect_invalid(truncated, sizeof(truncated));

    assert(!ku_utf8_decode_one(nullptr, 0U, nullptr, nullptr));
    return 0;
}

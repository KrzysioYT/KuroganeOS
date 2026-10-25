#pragma once
#include <stddef.h>
#include <stdint.h>

// Freestanding text path shared by the software renderer and the layout
// helpers. Invalid UTF-8 consumes exactly one byte and becomes U+FFFD.
// No heap, no locale and no reading past a terminating NUL.
namespace ku_text {
struct Scalar {
    uint32_t value;
    size_t bytes;
    bool valid;
};

inline Scalar next_z(const char* text) {
    if (text == nullptr || text[0] == '\0') return {0U, 0U, false};
    const uint8_t first = static_cast<uint8_t>(text[0]);
    if (first < 0x80U) return {first, 1U, true};
    size_t len = 0U;
    uint32_t cp = 0U;
    uint32_t minimum = 0U;
    if ((first & 0xe0U) == 0xc0U) {
        len = 2U; cp = first & 0x1fU; minimum = 0x80U;
    } else if ((first & 0xf0U) == 0xe0U) {
        len = 3U; cp = first & 0x0fU; minimum = 0x800U;
    } else if ((first & 0xf8U) == 0xf0U) {
        len = 4U; cp = first & 0x07U; minimum = 0x10000U;
    } else {
        return {0xfffdU, 1U, false};
    }
    for (size_t i = 1U; i < len; ++i) {
        const uint8_t byte = static_cast<uint8_t>(text[i]);
        if (byte == 0U || (byte & 0xc0U) != 0x80U)
            return {0xfffdU, 1U, false};
        cp = (cp << 6U) | (byte & 0x3fU);
    }
    if (cp < minimum || cp > 0x10ffffU ||
        (cp >= 0xd800U && cp <= 0xdfffU)) {
        return {0xfffdU, 1U, false};
    }
    return {cp, len, true};
}

enum class Accent : uint8_t { None, Acute, Dot, Stroke, Ogonek };
struct Glyph {
    char ascii;
    Accent accent;
};

// Compact, temporary Latin fallback for the existing 5x7 console raster.
// This is not a replacement for a licensed scalable Unicode font.
constexpr Glyph fallback(uint32_t cp) {
    switch (cp) {
        case 0x0104: case 0x0105: return {'A', Accent::Ogonek}; // Ą ą
        case 0x0106: case 0x0107: return {'C', Accent::Acute};  // Ć ć
        case 0x0118: case 0x0119: return {'E', Accent::Ogonek}; // Ę ę
        case 0x0141: case 0x0142: return {'L', Accent::Stroke}; // Ł ł
        case 0x0143: case 0x0144: return {'N', Accent::Acute};  // Ń ń
        case 0x00d3: case 0x00f3: return {'O', Accent::Acute};  // Ó ó
        case 0x015a: case 0x015b: return {'S', Accent::Acute};  // Ś ś
        case 0x0179: case 0x017a: return {'Z', Accent::Acute};  // Ź ź
        case 0x017b: case 0x017c: return {'Z', Accent::Dot};    // Ż ż
        default:
            return cp >= 0x20U && cp <= 0x7eU
                ? Glyph{static_cast<char>(cp), Accent::None}
                : Glyph{'?', Accent::None};
    }
}
} // namespace ku_text

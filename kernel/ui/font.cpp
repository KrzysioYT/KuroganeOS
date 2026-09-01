#include "font.hpp"

#include <stddef.h>

namespace ui::font {
namespace {

char display_character(char character) {
    if (character >= 'a' && character <= 'z') {
        return static_cast<char>(character - ('a' - 'A'));
    }
    return character;
}

int32_t base_advance(Face face, char character) {
    if (face == Face::Mono) return 6;

    if (face == Face::Display) {
        const char glyph = display_character(character);
        if (glyph == ' ') return 5;
        switch (glyph) {
            case 'I':
            case '.': case ',': case ':': case ';':
            case '!': case '\'': case '|':
                return 5;
            case 'M': case 'W': case '@': case '%': case '&':
                return 8;
            default:
                return 7;
        }
    }

    // UI Pixel is intentionally proportional while still using the same
    // deterministic 5x7 bitmap grid as the rest of the kernel UI.
    if (character == ' ') return 4;
    switch (character) {
        case 'i': case 'l': case 'I':
        case '.': case ',': case ':': case ';':
        case '!': case '\'': case '|':
            return 4;
        case 'M': case 'W': case 'm': case 'w':
        case '@': case '%': case '&':
            return 6;
        default:
            return 5;
    }
}

int32_t line_advance(Face face, uint32_t scale) {
    const int32_t base = face == Face::Display ? 9 : 8;
    return base * static_cast<int32_t>(scale);
}

void draw_glyph(
    Face face,
    int32_t x,
    int32_t y,
    char character,
    graphics::Color foreground,
    graphics::Color background,
    uint32_t scale,
    bool transparent) {
    if (face != Face::Display) {
        graphics::draw_char(
            x, y, character, foreground, background, scale, transparent);
        return;
    }

    // Display Pixel is a deliberately chunky title face: all-caps with a
    // one-pixel horizontal emboldening pass. The second pass is transparent so
    // it never erases pixels from the first pass when the caller requested an
    // opaque cell background.
    const char glyph = display_character(character);
    graphics::draw_char(
        x, y, glyph, foreground, background, scale, transparent);
    if (glyph != ' ') {
        graphics::draw_char(
            x + static_cast<int32_t>(scale),
            y,
            glyph,
            foreground,
            background,
            scale,
            true);
    }
}

} // namespace

int32_t measure(Face face, const char* text, uint32_t scale) {
    int32_t width = 0;
    int32_t line_width = 0;
    if (text == nullptr || scale == 0U) return 0;
    while (*text != '\0') {
        if (*text == '\n') {
            if (line_width > width) width = line_width;
            line_width = 0;
        } else {
            line_width += base_advance(face, *text) * static_cast<int32_t>(scale);
        }
        ++text;
    }
    return line_width > width ? line_width : width;
}

void draw(
    Face face,
    int32_t x,
    int32_t y,
    const char* text,
    graphics::Color foreground,
    graphics::Color background,
    uint32_t scale,
    bool transparent) {
    int32_t cursor_x = x;
    int32_t cursor_y = y;
    if (text == nullptr || scale == 0U) return;

    while (*text != '\0') {
        if (*text == '\n') {
            cursor_x = x;
            cursor_y += line_advance(face, scale);
            ++text;
            continue;
        }

        draw_glyph(
            face,
            cursor_x,
            cursor_y,
            *text,
            foreground,
            background,
            scale,
            transparent);

        cursor_x += base_advance(face, *text) * static_cast<int32_t>(scale);
        ++text;
    }
}

} // namespace ui::font

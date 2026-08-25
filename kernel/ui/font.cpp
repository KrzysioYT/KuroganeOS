#include "font.hpp"

#include <stddef.h>

namespace ui::font {
namespace {

int32_t base_advance(Face face, char character) {
    if (face == Face::Mono) return 6;
    if (character == ' ') return face == Face::Display ? 5 : 4;
    switch (character) {
        case 'i': case 'l': case 'I':
        case '.': case ',': case ':': case ';':
        case '!': case '\'': case '|':
            return face == Face::Display ? 5 : 4;
        case 'M': case 'W': case 'm': case 'w':
        case '@': case '%': case '&':
            return face == Face::Display ? 8 : 6;
        default:
            return face == Face::Display ? 7 : 5;
    }
}

int32_t line_advance(Face face, uint32_t scale) {
    const int32_t base = face == Face::Display ? 9 : 8;
    return base * static_cast<int32_t>(scale);
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

    // Kurogane 5 uses crisp condensed/technical typography. The previous
    // renderer performed a second complete glyph pass one pixel down/right to
    // fake a shadow. Besides looking too soft for Forged Steel, that doubled a
    // large part of software text rasterization on every compositor redraw.
    while (*text != '\0') {
        if (*text == '\n') {
            cursor_x = x;
            cursor_y += line_advance(face, scale);
            ++text;
            continue;
        }

        graphics::draw_char(
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

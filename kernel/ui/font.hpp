#pragma once

#include "../drivers/framebuffer.hpp"

#include <stdint.h>

namespace ui::font {

enum class Face : uint8_t {
    Ui = 0,
    Mono,
    Display,
};

int32_t measure(Face face, const char* text, uint32_t scale = 1U);
void draw(
    Face face,
    int32_t x,
    int32_t y,
    const char* text,
    graphics::Color foreground,
    graphics::Color background,
    uint32_t scale = 1U,
    bool transparent = true);

} // namespace ui::font

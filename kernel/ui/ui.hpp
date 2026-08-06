#pragma once

#include "../drivers/framebuffer.hpp"

#include <stdint.h>

namespace ui {

struct Rect {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
};

struct Theme {
    graphics::Color desktop;
    graphics::Color panel;
    graphics::Color panel_alt;
    graphics::Color border;
    graphics::Color text;
    graphics::Color text_muted;
    graphics::Color accent;
    graphics::Color danger;
};

const Theme& default_theme();
bool contains(const Rect& rectangle, int32_t x, int32_t y);
void desktop(const char* title);
void panel(const Rect& bounds, bool raised = true);
void window(const Rect& bounds, const char* title);
void label(const Rect& bounds, const char* text,
           graphics::Color color = 0, uint32_t scale = 1);
void button(const Rect& bounds, const char* text, bool selected = false);
void progress(const Rect& bounds, uint32_t value, uint32_t maximum);
void separator(int32_t x, int32_t y, int32_t width);
void taskbar(const char* status);

} // namespace ui

#pragma once

#include "../drivers/framebuffer.hpp"

#include <stddef.h>
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

enum class FluxControl : uint8_t {
    Minimize = 0,
    Expand,
    Dismiss,
};

const Theme& default_theme();
bool contains(const Rect& rectangle, int32_t x, int32_t y);
void desktop(const char* title);
void panel(const Rect& bounds, bool raised = true);
void window(const Rect& bounds, const char* title);
void flux_window(const Rect& bounds, const char* title, bool focused);
void flux_control(const Rect& bounds, FluxControl control, bool active = false);
void signal_spine(const Rect& bounds, size_t window_count, size_t focused_position);
void pulse_ribbon(const Rect& bounds, size_t window_count);
void pulse_item(const Rect& bounds, const char* title, bool focused, bool minimized);
void label(const Rect& bounds, const char* text,
           graphics::Color color = 0, uint32_t scale = 1);
void button(const Rect& bounds, const char* text, bool selected = false);
void progress(const Rect& bounds, uint32_t value, uint32_t maximum);
void separator(int32_t x, int32_t y, int32_t width);

// Legacy helper retained for old diagnostic surfaces. The 2.4 WindowManager
// no longer uses a conventional taskbar.
void taskbar(const char* status);

} // namespace ui

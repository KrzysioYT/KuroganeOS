#include "ui.hpp"

namespace ui {

namespace {
constexpr Theme kTheme = {
    graphics::rgb(19, 28, 46),
    graphics::rgb(30, 41, 59),
    graphics::rgb(15, 23, 42),
    graphics::rgb(71, 85, 105),
    graphics::rgb(241, 245, 249),
    graphics::rgb(148, 163, 184),
    graphics::rgb(249, 115, 22),
    graphics::rgb(239, 68, 68),
};
}

const Theme& default_theme() {
    return kTheme;
}

bool contains(const Rect& rectangle, int32_t x, int32_t y) {
    return x >= rectangle.x && y >= rectangle.y &&
           x < rectangle.x + rectangle.width &&
           y < rectangle.y + rectangle.height;
}

void desktop(const char* title) {
    if (!graphics::available()) {
        return;
    }
    graphics::clear(kTheme.desktop);
    graphics::fill_rect(0, 0, static_cast<int32_t>(graphics::width()), 36,
                        kTheme.panel_alt);
    graphics::fill_rect(0, 34, static_cast<int32_t>(graphics::width()), 2,
                        kTheme.accent);
    graphics::draw_text(12, 10, title ? title : "KUROGANE OS",
                        kTheme.text, kTheme.panel_alt, 2);
}

void panel(const Rect& bounds, bool raised) {
    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height,
                        raised ? kTheme.panel : kTheme.panel_alt);
    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height,
                        kTheme.border);
}

void window(const Rect& bounds, const char* title) {
    panel(bounds);
    graphics::fill_rect(bounds.x + 1, bounds.y + 1, bounds.width - 2, 28,
                        kTheme.panel_alt);
    graphics::fill_rect(bounds.x + 1, bounds.y + 28, bounds.width - 2, 2,
                        kTheme.accent);
    graphics::draw_text(bounds.x + 10, bounds.y + 8, title,
                        kTheme.text, kTheme.panel_alt, 2);
}

void label(const Rect& bounds, const char* text, graphics::Color color,
           uint32_t scale) {
    graphics::draw_text(bounds.x, bounds.y, text,
                        color == 0 ? kTheme.text : color,
                        kTheme.panel, scale, true);
}

void button(const Rect& bounds, const char* text, bool selected) {
    const auto background = selected ? kTheme.accent : kTheme.panel_alt;
    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height,
                        background);
    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height,
                        selected ? kTheme.text : kTheme.border);
    graphics::draw_text(bounds.x + 8, bounds.y + (bounds.height - 14) / 2,
                        text, kTheme.text, background, 2);
}

void progress(const Rect& bounds, uint32_t value, uint32_t maximum) {
    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height,
                        kTheme.panel_alt);
    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height,
                        kTheme.border);
    if (maximum == 0 || bounds.width <= 2 || bounds.height <= 2) {
        return;
    }
    if (value > maximum) {
        value = maximum;
    }
    const uint32_t interior = static_cast<uint32_t>(bounds.width - 2);
    const int32_t filled =
        static_cast<int32_t>((static_cast<uint64_t>(interior) * value) /
                             maximum);
    graphics::fill_rect(bounds.x + 1, bounds.y + 1, filled,
                        bounds.height - 2, kTheme.accent);
}

void separator(int32_t x, int32_t y, int32_t width) {
    graphics::fill_rect(x, y, width, 1, kTheme.border);
}

void taskbar(const char* status) {
    if (!graphics::available() || graphics::height() < 32) {
        return;
    }
    const int32_t y = static_cast<int32_t>(graphics::height() - 30);
    graphics::fill_rect(0, y, static_cast<int32_t>(graphics::width()), 30,
                        kTheme.panel_alt);
    graphics::fill_rect(0, y, static_cast<int32_t>(graphics::width()), 1,
                        kTheme.border);
    graphics::draw_text(10, y + 8, status ? status : "READY",
                        kTheme.text_muted, kTheme.panel_alt, 2);
}

} // namespace ui

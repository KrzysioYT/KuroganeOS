#include "ui.hpp"

namespace ui {

namespace {
constexpr Theme kTheme = {
    graphics::rgb(8, 10, 15),
    graphics::rgb(22, 26, 34),
    graphics::rgb(13, 16, 23),
    graphics::rgb(55, 64, 79),
    graphics::rgb(240, 244, 252),
    graphics::rgb(132, 145, 166),
    graphics::rgb(62, 220, 181),
    graphics::rgb(255, 82, 112),
};

constexpr graphics::Color kSignalViolet = graphics::rgb(116, 92, 246);
constexpr graphics::Color kSignalAmber = graphics::rgb(235, 174, 65);
constexpr graphics::Color kSurfaceShadow = graphics::rgb(3, 4, 7);
constexpr graphics::Color kSurfaceLifted = graphics::rgb(27, 32, 42);
constexpr graphics::Color kDesktopBand = graphics::rgb(12, 14, 20);

bool text_equals(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) return false;
    size_t index = 0U;
    while (left[index] != '\0' && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

bool text_starts_with(const char* text, const char* prefix) {
    if (text == nullptr || prefix == nullptr) return false;
    size_t index = 0U;
    while (prefix[index] != '\0') {
        if (text[index] != prefix[index]) return false;
        ++index;
    }
    return true;
}

void signal_node(int32_t x, int32_t y, graphics::Color color) {
    graphics::fill_rect(x, y, 7, 7, color);
    graphics::fill_rect(x + 2, y + 2, 3, 3, kTheme.desktop);
}

void corner_marks(const Rect& bounds, graphics::Color color) {
    constexpr int32_t mark = 10;
    graphics::fill_rect(bounds.x, bounds.y, mark, 2, color);
    graphics::fill_rect(bounds.x, bounds.y, 2, mark, color);
    graphics::fill_rect(bounds.x + bounds.width - mark, bounds.y, mark, 2, color);
    graphics::fill_rect(bounds.x + bounds.width - 2, bounds.y, 2, mark, color);
    graphics::fill_rect(bounds.x, bounds.y + bounds.height - 2, mark, 2, color);
    graphics::fill_rect(bounds.x, bounds.y + bounds.height - mark, 2, mark, color);
    graphics::fill_rect(bounds.x + bounds.width - mark, bounds.y + bounds.height - 2, mark, 2, color);
    graphics::fill_rect(bounds.x + bounds.width - 2, bounds.y + bounds.height - mark, 2, mark, color);
}
}

const Theme& default_theme() { return kTheme; }

bool contains(const Rect& rectangle, int32_t x, int32_t y) {
    return x >= rectangle.x && y >= rectangle.y &&
           x < rectangle.x + rectangle.width &&
           y < rectangle.y + rectangle.height;
}

void desktop(const char* title) {
    if (!graphics::available()) return;

    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t height = static_cast<int32_t>(graphics::height());
    graphics::clear(kTheme.desktop);

    // Kurogane Flux deliberately avoids a conventional menu bar, dock or
    // wallpaper metaphor. The workspace is identified by a narrow signal
    // spine and sparse machine-status geometry.
    graphics::fill_rect(0, 0, width, 2, kTheme.border);
    graphics::fill_rect(17, 12, 3, height > 64 ? height - 64 : height - 12, kTheme.accent);
    graphics::fill_rect(21, 12, 1, height > 64 ? height - 64 : height - 12, kSignalViolet);
    signal_node(15, 13, kSignalAmber);
    if (height > 160) signal_node(15, height / 2, kTheme.accent);
    if (height > 260) signal_node(15, height - 72, kSignalViolet);

    graphics::fill_rect(32, 8, width > 310 ? 270 : width - 44, 26, kDesktopBand);
    graphics::fill_rect(32, 33, width > 310 ? 104 : width - 44, 2, kTheme.accent);
    graphics::draw_text(42, 14, title ? title : "KUROGANE / FLUX", kTheme.text, kDesktopBand, 2, true);

    if (width > 520) {
        const int32_t status_x = width - 206;
        graphics::fill_rect(status_x, 10, 188, 20, kTheme.panel_alt);
        graphics::fill_rect(status_x, 10, 4, 20, kSignalViolet);
        graphics::draw_text(status_x + 12, 14, "FLUX // DEV PREVIEW", kTheme.text_muted, kTheme.panel_alt, 1, true);
    }

    if (height > 180 && width > 380) {
        graphics::fill_rect(width - 162, 52, 144, 2, kTheme.border);
        graphics::fill_rect(width - 92, 58, 74, 2, kSignalViolet);
        graphics::fill_rect(38, height - 51, 120, 1, kTheme.border);
    }
}

void panel(const Rect& bounds, bool raised) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    graphics::fill_rect(bounds.x + 4, bounds.y + 5, bounds.width, bounds.height, kSurfaceShadow);
    const auto background = raised ? kSurfaceLifted : kTheme.panel_alt;
    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, background);
    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height, kTheme.border);
    corner_marks(bounds, raised ? kTheme.accent : kSignalViolet);
}

void window(const Rect& bounds, const char* title) {
    panel(bounds, true);
    if (bounds.width <= 4 || bounds.height <= 30) return;

    graphics::fill_rect(bounds.x + 2, bounds.y + 2, 4, bounds.height - 4, kTheme.accent);
    graphics::fill_rect(bounds.x + 8, bounds.y + 29, bounds.width > 112 ? 96 : bounds.width - 16, 2, kSignalViolet);
    signal_node(bounds.x + 12, bounds.y + 9, kTheme.accent);
    graphics::draw_text(bounds.x + 28, bounds.y + 8, title ? title : "SURFACE", kTheme.text, kSurfaceLifted, 2, true);
}

void label(const Rect& bounds, const char* text, graphics::Color color, uint32_t scale) {
    graphics::draw_text(bounds.x, bounds.y, text, color == 0 ? kTheme.text : color, kTheme.panel, scale, true);
}

void button(const Rect& bounds, const char* text, bool selected) {
    const auto background = selected ? graphics::rgb(35, 61, 62) : kTheme.panel_alt;
    const auto signal = selected ? kTheme.accent : kTheme.border;
    graphics::fill_rect(bounds.x + 2, bounds.y + 2, bounds.width, bounds.height, kSurfaceShadow);
    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, background);
    graphics::fill_rect(bounds.x, bounds.y, 3, bounds.height, signal);
    graphics::fill_rect(bounds.x + 3, bounds.y, bounds.width - 3, 1, signal);

    const char* rendered = text;
    if (text_equals(text, "[]")) rendered = "<>";
    else if (text_equals(text, "-")) rendered = "_";
    else if (text_equals(text, "X")) rendered = "x";

    graphics::draw_text(bounds.x + 8, bounds.y + (bounds.height - 14) / 2, rendered ? rendered : "", kTheme.text, background, 2, true);
}

void progress(const Rect& bounds, uint32_t value, uint32_t maximum) {
    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, kTheme.panel_alt);
    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height, kTheme.border);
    if (maximum == 0U || bounds.width < 8 || bounds.height <= 4) return;
    if (value > maximum) value = maximum;

    constexpr uint32_t segments = 12U;
    const int32_t inner_width = bounds.width - 6;
    const int32_t gap = 2;
    int32_t segment_width = (inner_width - gap * static_cast<int32_t>(segments - 1U)) / static_cast<int32_t>(segments);
    if (segment_width < 1) segment_width = 1;
    const uint32_t active = static_cast<uint32_t>((static_cast<uint64_t>(value) * segments + maximum - 1U) / maximum);
    int32_t x = bounds.x + 3;
    for (uint32_t index = 0U; index < segments; ++index) {
        const graphics::Color signal = index < active
            ? (index + 2U >= segments ? kSignalAmber : kTheme.accent)
            : graphics::rgb(36, 43, 54);
        graphics::fill_rect(x, bounds.y + 3, segment_width, bounds.height - 6, signal);
        x += segment_width + gap;
        if (x >= bounds.x + bounds.width - 2) break;
    }
}

void separator(int32_t x, int32_t y, int32_t width) {
    if (width <= 0) return;
    graphics::fill_rect(x, y, width, 1, kTheme.border);
    if (width > 24) graphics::fill_rect(x, y, 18, 2, kTheme.accent);
}

void taskbar(const char* status) {
    if (!graphics::available() || graphics::height() < 32 || graphics::width() < 80) return;

    const int32_t screen_width = static_cast<int32_t>(graphics::width());
    const int32_t screen_height = static_cast<int32_t>(graphics::height());
    const int32_t x = 14;
    const int32_t y = screen_height - 27;
    const int32_t width = screen_width - 28;
    const char* text = status ? status : "FLUX READY";
    if (text_starts_with(text, "WINDOWS:")) {
        text = "FLUX WORKSPACE // FOCUS+DRAG // ALT+TAB CYCLE";
    }

    graphics::fill_rect(x + 3, y + 3, width, 20, kSurfaceShadow);
    graphics::fill_rect(x, y, width, 20, kTheme.panel_alt);
    graphics::draw_rect(x, y, width, 20, kTheme.border);
    graphics::fill_rect(x, y, 5, 20, kTheme.accent);
    graphics::fill_rect(x + 5, y, 34, 2, kSignalViolet);
    signal_node(x + width - 14, y + 6, kSignalAmber);
    graphics::draw_text(x + 13, y + 6, text, kTheme.text_muted, kTheme.panel_alt, 1, true);
}

} // namespace ui

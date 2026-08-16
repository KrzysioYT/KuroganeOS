#include "ui.hpp"

namespace ui {

namespace {
constexpr Theme kTheme = {
    graphics::rgb(5, 6, 8),       // desktop
    graphics::rgb(17, 19, 23),    // panel
    graphics::rgb(10, 12, 15),    // panel_alt
    graphics::rgb(52, 56, 63),    // border
    graphics::rgb(235, 237, 240), // text
    graphics::rgb(139, 144, 152), // text_muted
    graphics::rgb(222, 25, 45),   // accent
    graphics::rgb(255, 55, 68),   // danger
};

constexpr graphics::Color kRedBright = graphics::rgb(255, 35, 52);
constexpr graphics::Color kRedDeep = graphics::rgb(92, 12, 23);
constexpr graphics::Color kRedMuted = graphics::rgb(143, 26, 39);
constexpr graphics::Color kGraphite = graphics::rgb(25, 28, 33);
constexpr graphics::Color kGraphiteRaised = graphics::rgb(31, 34, 40);
constexpr graphics::Color kGraphiteFocused = graphics::rgb(35, 37, 43);
constexpr graphics::Color kSteel = graphics::rgb(82, 87, 96);
constexpr graphics::Color kInactiveSignal = graphics::rgb(42, 45, 51);
constexpr graphics::Color kSurfaceShadow = graphics::rgb(1, 2, 3);
constexpr graphics::Color kHeaderBand = graphics::rgb(13, 14, 17);

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

void copy_short_label(char* destination, size_t capacity, const char* source) {
    if (destination == nullptr || capacity == 0U) return;
    size_t index = 0U;
    if (source != nullptr) {
        while (index + 1U < capacity && source[index] != '\0') {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = '\0';
}

void signal_node(int32_t x, int32_t y, graphics::Color color) {
    graphics::fill_rect(x + 2, y, 4, 2, color);
    graphics::fill_rect(x, y + 2, 8, 4, color);
    graphics::fill_rect(x + 2, y + 6, 4, 2, color);
    graphics::fill_rect(x + 3, y + 3, 2, 2, kTheme.desktop);
}

void corner_marks(const Rect& bounds, graphics::Color color) {
    constexpr int32_t mark = 12;
    graphics::fill_rect(bounds.x, bounds.y, mark, 2, color);
    graphics::fill_rect(bounds.x, bounds.y, 2, mark, color);
    graphics::fill_rect(bounds.x + bounds.width - mark, bounds.y, mark, 2, color);
    graphics::fill_rect(bounds.x + bounds.width - 2, bounds.y, 2, mark, color);
    graphics::fill_rect(bounds.x, bounds.y + bounds.height - 2, mark, 2, color);
    graphics::fill_rect(bounds.x, bounds.y + bounds.height - mark, 2, mark, color);
    graphics::fill_rect(bounds.x + bounds.width - mark,
                        bounds.y + bounds.height - 2, mark, 2, color);
    graphics::fill_rect(bounds.x + bounds.width - 2,
                        bounds.y + bounds.height - mark, 2, mark, color);
}

void control_minimize(const Rect& bounds, graphics::Color color) {
    const int32_t y = bounds.y + bounds.height / 2 + 3;
    graphics::fill_rect(bounds.x + 6, y, bounds.width - 12, 2, color);
}

void control_expand(const Rect& bounds, graphics::Color color) {
    const int32_t left = bounds.x + 6;
    const int32_t top = bounds.y + 5;
    const int32_t right = bounds.x + bounds.width - 7;
    const int32_t bottom = bounds.y + bounds.height - 6;
    graphics::fill_rect(left, top, 7, 2, color);
    graphics::fill_rect(left, top, 2, 7, color);
    graphics::fill_rect(right - 5, bottom, 7, 2, color);
    graphics::fill_rect(right, bottom - 5, 2, 7, color);
}

void control_dismiss(const Rect& bounds, graphics::Color color) {
    const int32_t left = bounds.x + 7;
    const int32_t right = bounds.x + bounds.width - 8;
    const int32_t top = bounds.y + 5;
    const int32_t bottom = bounds.y + bounds.height - 6;
    for (int32_t offset = 0; offset < 2; ++offset) {
        for (int32_t step = 0; step <= bottom - top; ++step) {
            const int32_t width = right - left;
            const int32_t diagonal = width == 0 ? 0 : (step * width) / (bottom - top + 1);
            graphics::put_pixel(left + diagonal + offset, top + step, color);
            graphics::put_pixel(right - diagonal - offset, top + step, color);
        }
    }
}

void red_chevron(int32_t center_x, int32_t y) {
    graphics::fill_rect(center_x - 18, y, 12, 2, kRedMuted);
    graphics::fill_rect(center_x - 10, y + 2, 10, 2, kTheme.accent);
    graphics::fill_rect(center_x, y + 2, 10, 2, kTheme.accent);
    graphics::fill_rect(center_x + 6, y, 12, 2, kRedMuted);
    graphics::fill_rect(center_x - 2, y + 4, 4, 5, kRedBright);
}
} // namespace

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

    graphics::fill_rect(0, 0, width, 2, kTheme.border);
    graphics::fill_rect(0, 2, width, 1, kRedDeep);

    // Signal spine: thin, asymmetric and deliberately unlike a taskbar/dock.
    graphics::fill_rect(17, 12, 2, height > 64 ? height - 64 : height - 12,
                        kTheme.accent);
    graphics::fill_rect(20, 12, 1, height > 64 ? height - 64 : height - 12,
                        kRedDeep);
    signal_node(14, 13, kRedBright);

    const int32_t brand_width = width > 420 ? 330 : width - 44;
    graphics::fill_rect(32, 8, brand_width, 27, kHeaderBand);
    graphics::fill_rect(32, 34, brand_width > 142 ? 142 : brand_width, 2,
                        kTheme.accent);
    graphics::draw_text(42, 14, title ? title : "KUROGANE / RED FLUX",
                        kTheme.text, kHeaderBand, 2, true);

    if (width > 560) {
        const int32_t status_x = width - 226;
        graphics::fill_rect(status_x + 3, 13, 208, 19, kSurfaceShadow);
        graphics::fill_rect(status_x, 10, 208, 20, kTheme.panel_alt);
        graphics::fill_rect(status_x, 10, 4, 20, kTheme.accent);
        graphics::fill_rect(status_x + 4, 10, 34, 1, kRedBright);
        graphics::draw_text(status_x + 13, 14, "RED FLUX // DESKTOP",
                            kTheme.text_muted, kTheme.panel_alt, 1, true);
    }

    if (width > 620) red_chevron(width / 2, 18);
    if (height > 180 && width > 380) {
        graphics::fill_rect(width - 178, 53, 160, 1, kTheme.border);
        graphics::fill_rect(width - 96, 58, 78, 2, kRedDeep);
        graphics::fill_rect(38, height - 51, 120, 1, kTheme.border);
    }
}

void panel(const Rect& bounds, bool raised) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    graphics::fill_rect(bounds.x + 4, bounds.y + 5,
                        bounds.width, bounds.height, kSurfaceShadow);
    const auto background = raised ? kGraphiteRaised : kTheme.panel_alt;
    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, background);
    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height, kTheme.border);
    corner_marks(bounds, raised ? kTheme.accent : kRedDeep);
}

void flux_window(const Rect& bounds, const char* title, bool focused) {
    if (bounds.width <= 0 || bounds.height <= 0) return;

    graphics::fill_rect(bounds.x + 6, bounds.y + 7,
                        bounds.width, bounds.height, kSurfaceShadow);
    const graphics::Color background = focused ? kGraphiteFocused : kGraphite;
    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, background);
    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height,
                        focused ? kRedMuted : kTheme.border);

    const graphics::Color signal = focused ? kRedBright : kSteel;
    graphics::fill_rect(bounds.x, bounds.y, 4, bounds.height, signal);
    graphics::fill_rect(bounds.x + 4, bounds.y,
                        bounds.width > 126 ? 118 : bounds.width - 8, 2, signal);
    graphics::fill_rect(bounds.x + 11, bounds.y + 32,
                        bounds.width > 146 ? 130 : bounds.width - 22, 1,
                        focused ? kTheme.accent : kTheme.border);
    signal_node(bounds.x + 12, bounds.y + 12, signal);
    graphics::draw_text(bounds.x + 30, bounds.y + 10,
                        title ? title : "SURFACE",
                        focused ? kTheme.text : kTheme.text_muted,
                        background, 2, true);

    const graphics::Color grip = focused ? kTheme.accent : kTheme.border;
    graphics::fill_rect(bounds.x + bounds.width - 14,
                        bounds.y + bounds.height - 3, 12, 1, grip);
    graphics::fill_rect(bounds.x + bounds.width - 3,
                        bounds.y + bounds.height - 14, 1, 12, grip);
}

void window(const Rect& bounds, const char* title) {
    flux_window(bounds, title, false);
}

void flux_control(const Rect& bounds, FluxControl control, bool active) {
    if (bounds.width <= 8 || bounds.height <= 8) return;
    const graphics::Color background = active ? graphics::rgb(42, 31, 34) : kGraphite;
    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, background);
    graphics::fill_rect(bounds.x, bounds.y + bounds.height - 1,
                        bounds.width, 1,
                        active ? kTheme.accent : kTheme.border);

    switch (control) {
        case FluxControl::Minimize:
            control_minimize(bounds, active ? kTheme.text : kTheme.text_muted);
            break;
        case FluxControl::Expand:
            control_expand(bounds, active ? kRedBright : kSteel);
            break;
        case FluxControl::Dismiss:
            control_dismiss(bounds, active ? kTheme.danger : kRedMuted);
            break;
    }
}

void signal_spine(const Rect& bounds, size_t window_count, size_t focused_position) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const int32_t center_x = bounds.x + bounds.width / 2;
    graphics::fill_rect(center_x, bounds.y, 1, bounds.height, kTheme.border);
    graphics::fill_rect(center_x + 3, bounds.y, 1, bounds.height, kRedDeep);

    const size_t visible = window_count < 10U ? window_count : 10U;
    if (visible == 0U) {
        signal_node(center_x - 3, bounds.y + 10, kInactiveSignal);
        return;
    }

    const int32_t available = bounds.height > 24 ? bounds.height - 24 : bounds.height;
    const int32_t step = visible > 1U
        ? available / static_cast<int32_t>(visible - 1U) : 0;
    for (size_t index = 0U; index < visible; ++index) {
        const int32_t y = bounds.y + 8 + static_cast<int32_t>(index) * step;
        const graphics::Color signal = index == focused_position
            ? kRedBright
            : (index % 2U == 0U ? kRedDeep : kInactiveSignal);
        signal_node(center_x - 3, y, signal);
    }
}

void pulse_ribbon(const Rect& bounds, size_t window_count) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    graphics::fill_rect(bounds.x + 4, bounds.y + 4,
                        bounds.width, bounds.height, kSurfaceShadow);
    graphics::fill_rect(bounds.x, bounds.y,
                        bounds.width, bounds.height, kTheme.panel_alt);
    graphics::draw_rect(bounds.x, bounds.y,
                        bounds.width, bounds.height, kTheme.border);
    graphics::fill_rect(bounds.x, bounds.y, 5, bounds.height, kTheme.accent);
    graphics::fill_rect(bounds.x + 5, bounds.y, 42, 2, kRedBright);

    const int32_t pulse_x = bounds.x + bounds.width - 18;
    const int32_t pulse_y = bounds.y + bounds.height / 2 - 4;
    signal_node(pulse_x, pulse_y,
                window_count == 0U ? kInactiveSignal : kRedBright);
}

void pulse_item(const Rect& bounds, const char* title, bool focused, bool minimized) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const graphics::Color background = focused
        ? graphics::rgb(50, 22, 27)
        : (minimized ? graphics::rgb(11, 12, 15) : kGraphite);
    const graphics::Color signal = focused
        ? kRedBright
        : (minimized ? kInactiveSignal : kSteel);
    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, background);
    graphics::fill_rect(bounds.x, bounds.y, 3, bounds.height, signal);
    graphics::fill_rect(bounds.x + 3, bounds.y + bounds.height - 1,
                        bounds.width - 3, 1, signal);

    char label[17];
    copy_short_label(label, sizeof(label), title ? title : "SURFACE");
    graphics::draw_text(bounds.x + 9, bounds.y + 6, label,
                        minimized ? kTheme.text_muted : kTheme.text,
                        background, 1, true);
}

void label(const Rect& bounds, const char* text,
           graphics::Color color, uint32_t scale) {
    graphics::draw_text(bounds.x, bounds.y, text,
                        color == 0 ? kTheme.text : color,
                        kTheme.panel, scale, true);
}

void button(const Rect& bounds, const char* text, bool selected) {
    const auto background = selected ? graphics::rgb(55, 20, 26) : kTheme.panel_alt;
    const auto signal = selected ? kRedBright : kTheme.border;
    graphics::fill_rect(bounds.x + 2, bounds.y + 2,
                        bounds.width, bounds.height, kSurfaceShadow);
    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, background);
    graphics::fill_rect(bounds.x, bounds.y, 3, bounds.height, signal);
    graphics::fill_rect(bounds.x + 3, bounds.y, bounds.width - 3, 1, signal);

    const char* rendered = text;
    if (text_equals(text, "[]")) rendered = "<>";
    else if (text_equals(text, "-")) rendered = "_";
    else if (text_equals(text, "X")) rendered = "x";

    graphics::draw_text(bounds.x + 8,
                        bounds.y + (bounds.height - 14) / 2,
                        rendered ? rendered : "",
                        kTheme.text, background, 2, true);
}

void progress(const Rect& bounds, uint32_t value, uint32_t maximum) {
    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height,
                        kTheme.panel_alt);
    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height,
                        kTheme.border);
    if (maximum == 0U || bounds.width < 8 || bounds.height <= 4) return;
    if (value > maximum) value = maximum;

    constexpr uint32_t segments = 12U;
    const int32_t inner_width = bounds.width - 6;
    const int32_t gap = 2;
    int32_t segment_width =
        (inner_width - gap * static_cast<int32_t>(segments - 1U)) /
        static_cast<int32_t>(segments);
    if (segment_width < 1) segment_width = 1;
    const uint32_t active = static_cast<uint32_t>(
        (static_cast<uint64_t>(value) * segments + maximum - 1U) / maximum);
    int32_t x = bounds.x + 3;
    for (uint32_t index = 0U; index < segments; ++index) {
        const graphics::Color signal = index < active
            ? (index + 2U >= segments ? kRedBright : kTheme.accent)
            : graphics::rgb(35, 38, 44);
        graphics::fill_rect(x, bounds.y + 3,
                            segment_width, bounds.height - 6, signal);
        x += segment_width + gap;
        if (x >= bounds.x + bounds.width - 2) break;
    }
}

void separator(int32_t x, int32_t y, int32_t width) {
    if (width <= 0) return;
    graphics::fill_rect(x, y, width, 1, kTheme.border);
    if (width > 24) graphics::fill_rect(x, y, 22, 2, kTheme.accent);
}

void taskbar(const char* status) {
    if (!graphics::available() || graphics::height() < 32 ||
        graphics::width() < 80) return;

    const int32_t screen_width = static_cast<int32_t>(graphics::width());
    const int32_t screen_height = static_cast<int32_t>(graphics::height());
    const int32_t x = 14;
    const int32_t y = screen_height - 27;
    const int32_t width = screen_width - 28;
    const char* text = status ? status : "RED FLUX READY";
    if (text_starts_with(text, "WINDOWS:")) {
        text = "LEGACY SURFACE // RED FLUX COMPATIBILITY";
    }

    graphics::fill_rect(x + 3, y + 3, width, 20, kSurfaceShadow);
    graphics::fill_rect(x, y, width, 20, kTheme.panel_alt);
    graphics::draw_rect(x, y, width, 20, kTheme.border);
    graphics::fill_rect(x, y, 5, 20, kTheme.accent);
    graphics::fill_rect(x + 5, y, 38, 2, kRedBright);
    signal_node(x + width - 14, y + 6, kRedMuted);
    graphics::draw_text(x + 13, y + 6, text,
                        kTheme.text_muted, kTheme.panel_alt, 1, true);
}

} // namespace ui

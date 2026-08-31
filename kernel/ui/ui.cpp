#include "ui.hpp"

#include "../../common/version.h"

namespace ui {

namespace {
constexpr Theme kTheme = {
    graphics::rgb(4, 5, 7),       // desktop
    graphics::rgb(18, 19, 22),    // panel
    graphics::rgb(10, 11, 14),    // panel_alt
    graphics::rgb(55, 58, 64),    // border
    graphics::rgb(238, 239, 242), // text
    graphics::rgb(145, 149, 156), // text_muted
    graphics::rgb(220, 22, 40),   // accent
    graphics::rgb(255, 54, 66),   // danger
};

constexpr graphics::Color kRedBright = graphics::rgb(255, 34, 48);
constexpr graphics::Color kRedHot = graphics::rgb(239, 20, 36);
constexpr graphics::Color kRedDeep = graphics::rgb(83, 10, 20);
constexpr graphics::Color kRedMuted = graphics::rgb(139, 24, 36);
constexpr graphics::Color kGraphite = graphics::rgb(25, 27, 31);
constexpr graphics::Color kGraphiteRaised = graphics::rgb(31, 33, 38);
constexpr graphics::Color kGraphiteFocused = graphics::rgb(38, 35, 40);
constexpr graphics::Color kSteel = graphics::rgb(85, 89, 96);
constexpr graphics::Color kInactiveSignal = graphics::rgb(43, 45, 50);
constexpr graphics::Color kSurfaceShadow = graphics::rgb(1, 2, 3);
constexpr graphics::Color kHeaderBand = graphics::rgb(12, 13, 16);
constexpr graphics::Color kDockSurface = graphics::rgb(14, 15, 18);
constexpr graphics::Color kDockRaised = graphics::rgb(27, 28, 33);

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

int32_t iabs(int32_t value) {
    return value < 0 ? -value : value;
}

void line(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
          graphics::Color color) {
    const int32_t dx = iabs(x1 - x0);
    const int32_t sx = x0 < x1 ? 1 : -1;
    const int32_t dy = -iabs(y1 - y0);
    const int32_t sy = y0 < y1 ? 1 : -1;
    int32_t error = dx + dy;
    for (;;) {
        graphics::put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        const int32_t doubled = error * 2;
        if (doubled >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += sy;
        }
    }
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
    line(bounds.x + 7, bounds.y + 5,
         bounds.x + bounds.width - 8, bounds.y + bounds.height - 6, color);
    line(bounds.x + bounds.width - 8, bounds.y + 5,
         bounds.x + 7, bounds.y + bounds.height - 6, color);
    line(bounds.x + 8, bounds.y + 5,
         bounds.x + bounds.width - 7, bounds.y + bounds.height - 6, color);
    line(bounds.x + bounds.width - 7, bounds.y + 5,
         bounds.x + 8, bounds.y + bounds.height - 6, color);
}

void red_chevron(int32_t center_x, int32_t y) {
    graphics::fill_rect(center_x - 18, y, 12, 2, kRedMuted);
    graphics::fill_rect(center_x - 10, y + 2, 10, 2, kTheme.accent);
    graphics::fill_rect(center_x, y + 2, 10, 2, kTheme.accent);
    graphics::fill_rect(center_x + 6, y, 12, 2, kRedMuted);
    graphics::fill_rect(center_x - 2, y + 4, 4, 5, kRedBright);
}

void backdrop_gradient() {
    if (!graphics::available()) return;
    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t height = static_cast<int32_t>(graphics::height());
    graphics::clear(kTheme.desktop);
    for (int32_t y = 0; y < height; y += 4) {
        const uint32_t position = height > 0
            ? static_cast<uint32_t>((static_cast<uint64_t>(y) * 9U) /
                                    static_cast<uint32_t>(height))
            : 0U;
        const uint8_t shade = static_cast<uint8_t>(6U + position);
        graphics::fill_rect(
            0, y, width, 4,
            graphics::rgb(shade, shade, static_cast<uint8_t>(shade + 2U)));
    }
    graphics::fill_rect(0, 0, width, 3, kRedDeep);
}

void brand_mark(int32_t center_x, int32_t center_y, int32_t scale,
                graphics::Color primary, graphics::Color secondary) {
    if (scale < 1) scale = 1;
    const int32_t r = 15 * scale;
    // Angular outer crest inspired by the wolf/ring language of the logo.
    line(center_x - r, center_y, center_x, center_y - r, secondary);
    line(center_x, center_y - r, center_x + r, center_y, secondary);
    line(center_x + r, center_y, center_x, center_y + r, secondary);
    line(center_x, center_y + r, center_x - r, center_y, secondary);
    line(center_x - r + scale * 2, center_y,
         center_x, center_y - r + scale * 2, primary);
    line(center_x, center_y - r + scale * 2,
         center_x + r - scale * 2, center_y, primary);

    // Inner asymmetric shard: a compact Kurogane signature rather than a
    // Windows/macOS/Linux-derived icon.
    line(center_x - 5 * scale, center_y - 7 * scale,
         center_x - 5 * scale, center_y + 7 * scale, primary);
    line(center_x - 4 * scale, center_y,
         center_x + 6 * scale, center_y - 7 * scale, primary);
    line(center_x - 4 * scale, center_y,
         center_x + 7 * scale, center_y + 7 * scale, kRedBright);
    graphics::fill_rect(center_x - scale, center_y - scale,
                        scale * 3, scale * 3, kRedBright);
}

void dock_icon(const Rect& bounds, DockIcon icon,
               graphics::Color foreground, graphics::Color accent) {
    const int32_t cx = bounds.x + bounds.width / 2;
    const int32_t cy = bounds.y + bounds.height / 2;
    switch (icon) {
        case DockIcon::Home:
            brand_mark(cx, cy, 1, accent, foreground);
            break;
        case DockIcon::Terminal:
            line(cx - 9, cy - 7, cx - 2, cy, foreground);
            line(cx - 9, cy + 7, cx - 2, cy, foreground);
            graphics::fill_rect(cx + 1, cy + 6, 10, 2, accent);
            break;
        case DockIcon::Files:
            graphics::fill_rect(cx - 10, cy - 7, 8, 4, accent);
            graphics::draw_rect(cx - 11, cy - 4, 22, 15, foreground, 2U);
            graphics::fill_rect(cx - 8, cy, 16, 2, kDockRaised);
            break;
        case DockIcon::Monitor:
            graphics::fill_rect(cx - 10, cy + 3, 4, 7, foreground);
            graphics::fill_rect(cx - 3, cy - 2, 4, 12, accent);
            graphics::fill_rect(cx + 4, cy - 8, 4, 18, foreground);
            break;
        case DockIcon::Settings:
            graphics::draw_rect(cx - 8, cy - 8, 16, 16, foreground, 2U);
            graphics::fill_rect(cx - 2, cy - 11, 4, 22, accent);
            graphics::fill_rect(cx - 11, cy - 2, 22, 4, accent);
            graphics::fill_rect(cx - 2, cy - 2, 4, 4, kDockSurface);
            break;
        case DockIcon::About:
            graphics::fill_rect(cx - 2, cy - 7, 4, 4, accent);
            graphics::fill_rect(cx - 2, cy, 4, 11, foreground);
            break;
    }
}
} // namespace

const Theme& default_theme() { return kTheme; }

bool contains(const Rect& rectangle, int32_t x, int32_t y) {
    return x >= rectangle.x && y >= rectangle.y &&
           x < rectangle.x + rectangle.width &&
           y < rectangle.y + rectangle.height;
}

void boot_splash(const char* stage, uint32_t progress_value) {
    if (!graphics::available()) return;
    graphics::reset_clip();
    graphics::reset_text_scale_limit();
    backdrop_gradient();
    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t height = static_cast<int32_t>(graphics::height());
    const int32_t cx = width / 2;
    const int32_t cy = height / 2;

    brand_mark(cx, cy - 92, 4, kRedBright, kRedDeep);
    graphics::draw_text(cx - 114, cy - 20, "KUROGANEOS",
                        kTheme.text, kTheme.desktop, 3U, true);
    graphics::draw_text(cx - 62, cy + 18, KUROGANE_VERSION_STRING,
                        kTheme.text_muted, kTheme.desktop, 2U, true);
    graphics::draw_text(cx - 108, cy + 54,
                        stage ? stage : "INITIALIZING RED FLUX",
                        kTheme.text_muted, kTheme.desktop, 1U, true);

    const int32_t bar_width = width > 520 ? 360 : width - 120;
    const int32_t bar_x = (width - bar_width) / 2;
    const int32_t bar_y = cy + 82;
    graphics::fill_rect(bar_x, bar_y, bar_width, 4, kInactiveSignal);
    if (progress_value > 100U) progress_value = 100U;
    const int32_t active = static_cast<int32_t>(
        (static_cast<uint64_t>(bar_width) * progress_value) / 100U);
    if (active > 0) graphics::fill_rect(bar_x, bar_y, active, 4, kRedHot);
    red_chevron(cx, bar_y + 22);
}

void login_backdrop(const char* status) {
    if (!graphics::available()) return;
    graphics::reset_clip();
    graphics::reset_text_scale_limit();
    backdrop_gradient();
    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t height = static_cast<int32_t>(graphics::height());
    const int32_t cx = width / 2;

    brand_mark(cx, 132, 4, kRedBright, kRedDeep);
    graphics::draw_text(cx - 108, 208, "KUROGANEOS",
                        kTheme.text, kTheme.desktop, 3U, true);
    graphics::draw_text(cx - 114, 250, "RED FLUX / SESSION",
                        kTheme.text_muted, kTheme.desktop, 1U, true);
    if (status != nullptr) {
        graphics::draw_text(cx - 96, height - 48, status,
                            kTheme.text_muted, kTheme.desktop, 1U, true);
    }
    graphics::fill_rect(0, height - 3, width, 3, kRedDeep);
}

void desktop(const char* title) {
    if (!graphics::available()) return;

    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t height = static_cast<int32_t>(graphics::height());
    backdrop_gradient();

    // Quiet geometric wallpaper: a large low-contrast Kurogane mark creates
    // depth without imitating a conventional OS wallpaper/taskbar layout.
    if (width > 760 && height > 520) {
        brand_mark(width / 2, height / 2 - 28, 9,
                   graphics::rgb(48, 12, 18), graphics::rgb(27, 15, 19));
    }

    // Top identity rail.
    const int32_t brand_width = width > 480 ? 360 : width - 36;
    graphics::fill_rect(18, 10, brand_width, 31, kHeaderBand);
    graphics::fill_rect(18, 10, 4, 31, kTheme.accent);
    graphics::fill_rect(22, 39, brand_width > 156 ? 156 : brand_width - 4,
                        2, kRedBright);
    brand_mark(40, 25, 1, kRedBright, kRedMuted);
    graphics::draw_text(61, 17, title ? title : "KUROGANE / RED FLUX",
                        kTheme.text, kHeaderBand, 2U, true);

    if (width > 640) {
        const int32_t status_x = width - 238;
        graphics::fill_rect(status_x + 3, 14, 218, 23, kSurfaceShadow);
        graphics::fill_rect(status_x, 11, 218, 24, kTheme.panel_alt);
        graphics::fill_rect(status_x, 11, 4, 24, kTheme.accent);
        graphics::draw_text(status_x + 14, 19, "SESSION / RED FLUX 3.2",
                            kTheme.text_muted, kTheme.panel_alt, 1U, true);
    }

    // Signature rail retained from early Flux, now reduced to a background
    // status element rather than a competing navigation surface.
    graphics::fill_rect(17, 58, 2, height > 142 ? height - 142 : 1,
                        kRedDeep);
    graphics::fill_rect(20, 58, 1, height > 142 ? height - 142 : 1,
                        kTheme.border);
    signal_node(14, 59, kRedBright);

    if (width > 620) red_chevron(width / 2, 24);
}

void panel(const Rect& bounds, bool raised) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    graphics::fill_rect(bounds.x + 5, bounds.y + 6,
                        bounds.width, bounds.height, kSurfaceShadow);
    const auto background = raised ? kGraphiteRaised : kTheme.panel_alt;
    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, background);
    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height, kTheme.border);
    corner_marks(bounds, raised ? kTheme.accent : kRedDeep);
}

void flux_window(const Rect& bounds, const char* title, bool focused) {
    if (bounds.width <= 0 || bounds.height <= 0) return;

    graphics::fill_rect(bounds.x + 7, bounds.y + 8,
                        bounds.width, bounds.height, kSurfaceShadow);
    const graphics::Color background = focused ? kGraphiteFocused : kGraphite;
    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, background);
    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height,
                        focused ? kRedMuted : kTheme.border);

    const graphics::Color signal = focused ? kRedBright : kSteel;
    graphics::fill_rect(bounds.x, bounds.y, 4, bounds.height, signal);
    graphics::fill_rect(bounds.x + 4, bounds.y,
                        bounds.width > 142 ? 134 : bounds.width - 8, 2, signal);
    graphics::fill_rect(bounds.x + 12, bounds.y + 34,
                        bounds.width > 162 ? 146 : bounds.width - 24, 1,
                        focused ? kTheme.accent : kTheme.border);
    signal_node(bounds.x + 13, bounds.y + 13, signal);
    graphics::draw_text(bounds.x + 32, bounds.y + 11,
                        title ? title : "SURFACE",
                        focused ? kTheme.text : kTheme.text_muted,
                        background, 2U, true);

    const graphics::Color grip = focused ? kTheme.accent : kTheme.border;
    line(bounds.x + bounds.width - 16, bounds.y + bounds.height - 3,
         bounds.x + bounds.width - 3, bounds.y + bounds.height - 16, grip);
    line(bounds.x + bounds.width - 10, bounds.y + bounds.height - 3,
         bounds.x + bounds.width - 3, bounds.y + bounds.height - 10, grip);
}

void window(const Rect& bounds, const char* title) {
    flux_window(bounds, title, false);
}

void flux_control(const Rect& bounds, FluxControl control, bool active) {
    if (bounds.width <= 8 || bounds.height <= 8) return;
    const graphics::Color background = active ? graphics::rgb(45, 27, 31) : kGraphite;
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

void dock_bar(const Rect& bounds, size_t running_count) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    graphics::fill_rect(bounds.x + 6, bounds.y + 7,
                        bounds.width, bounds.height, kSurfaceShadow);
    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height,
                        kDockSurface);
    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height,
                        kTheme.border);
    graphics::fill_rect(bounds.x + 12, bounds.y, 58, 2, kRedBright);
    graphics::fill_rect(bounds.x + bounds.width - 70, bounds.y,
                        58, 2, running_count == 0U ? kRedDeep : kRedMuted);
    red_chevron(bounds.x + bounds.width / 2, bounds.y + 5);
}

void dock_item(const Rect& bounds, DockIcon icon, bool running, bool focused) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const graphics::Color background = focused
        ? graphics::rgb(54, 21, 27)
        : (running ? kDockRaised : kDockSurface);
    const graphics::Color border = focused
        ? kRedBright
        : (running ? kSteel : graphics::rgb(38, 40, 45));
    graphics::fill_rect(bounds.x + 2, bounds.y + 3,
                        bounds.width, bounds.height, kSurfaceShadow);
    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, background);
    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height, border);
    if (focused) graphics::fill_rect(bounds.x + 8, bounds.y, bounds.width - 16, 2, kRedBright);
    dock_icon(bounds, icon,
              focused ? kTheme.text : kTheme.text_muted,
              focused || running ? kRedHot : kRedMuted);
    if (running) {
        graphics::fill_rect(bounds.x + bounds.width / 2 - 3,
                            bounds.y + bounds.height - 5, 7, 2,
                            focused ? kRedBright : kRedMuted);
    }
}

void dock_task(const Rect& bounds, const char* title, bool focused, bool minimized) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const graphics::Color background = focused
        ? graphics::rgb(49, 20, 25)
        : (minimized ? graphics::rgb(10, 11, 13) : kGraphite);
    const graphics::Color signal = focused
        ? kRedBright
        : (minimized ? kInactiveSignal : kSteel);
    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, background);
    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height,
                        focused ? kRedMuted : kTheme.border);
    graphics::fill_rect(bounds.x, bounds.y, 3, bounds.height, signal);

    char task_label[15];
    copy_short_label(task_label, sizeof(task_label), title ? title : "APP");
    graphics::draw_text(bounds.x + 8, bounds.y + 9, task_label,
                        minimized ? kTheme.text_muted : kTheme.text,
                        background, 1U, true);
}

void pulse_ribbon(const Rect& bounds, size_t window_count) {
    dock_bar(bounds, window_count);
}

void pulse_item(const Rect& bounds, const char* title, bool focused, bool minimized) {
    dock_task(bounds, title, focused, minimized);
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
                        kTheme.text, background, 2U, true);
}

void input_field(const Rect& bounds, const char* text, bool focused) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const graphics::Color background = focused
        ? graphics::rgb(24, 18, 22) : kTheme.panel_alt;
    const graphics::Color border = focused ? kRedBright : kTheme.border;
    graphics::fill_rect(bounds.x + 3, bounds.y + 3,
                        bounds.width, bounds.height, kSurfaceShadow);
    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, background);
    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height, border);
    graphics::fill_rect(bounds.x, bounds.y, 4, bounds.height,
                        focused ? kRedBright : kRedDeep);
    graphics::draw_text(bounds.x + 12, bounds.y + (bounds.height - 8) / 2,
                        text ? text : "", kTheme.text, background, 1U, true);
}

void list_row(
    const Rect& bounds, const char* text, bool selected, bool disabled) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const graphics::Color background = selected
        ? graphics::rgb(43, 20, 25) : kGraphite;
    const graphics::Color signal = selected
        ? kRedBright : (disabled ? kInactiveSignal : kSteel);
    const graphics::Color foreground = disabled ? kTheme.text_muted : kTheme.text;
    graphics::fill_rect(bounds.x + 3, bounds.y + 3,
                        bounds.width, bounds.height, kSurfaceShadow);
    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, background);
    graphics::draw_rect(bounds.x, bounds.y, bounds.width, bounds.height,
                        selected ? kRedMuted : kTheme.border);
    graphics::fill_rect(bounds.x, bounds.y, 4, bounds.height, signal);
    if (selected && bounds.width > 48) {
        graphics::fill_rect(bounds.x + 12, bounds.y, 34, 2, kRedBright);
    }
    graphics::draw_text(bounds.x + 14, bounds.y + (bounds.height - 8) / 2,
                        text ? text : "", foreground, background, 1U, true);
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
        text = "LEGACY SURFACE / RED FLUX COMPATIBILITY";
    }

    graphics::fill_rect(x + 3, y + 3, width, 20, kSurfaceShadow);
    graphics::fill_rect(x, y, width, 20, kTheme.panel_alt);
    graphics::draw_rect(x, y, width, 20, kTheme.border);
    graphics::fill_rect(x, y, 5, 20, kTheme.accent);
    graphics::fill_rect(x + 5, y, 38, 2, kRedBright);
    signal_node(x + width - 14, y + 6, kRedMuted);
    graphics::draw_text(x + 13, y + 6, text,
                        kTheme.text_muted, kTheme.panel_alt, 1U, true);
}

} // namespace ui

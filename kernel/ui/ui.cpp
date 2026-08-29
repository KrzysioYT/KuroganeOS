#include "ui.hpp"

#include "../../common/version.h"

namespace ui {
namespace {

constexpr Theme kTheme = {
    graphics::rgb(11, 16, 21),   // desktop
    graphics::rgb(23, 28, 33),   // panel
    graphics::rgb(31, 36, 41),   // panel_alt
    graphics::rgb(49, 56, 63),   // border
    graphics::rgb(240, 242, 244),// text
    graphics::rgb(141, 150, 159),// text_muted
    graphics::rgb(192, 51, 47),  // accent
    graphics::rgb(220, 62, 55),  // danger
};

constexpr graphics::Color kDesktopTop = graphics::rgb(14, 20, 26);
constexpr graphics::Color kDesktopBottom = graphics::rgb(7, 11, 15);
constexpr graphics::Color kSurfaceInset = graphics::rgb(17, 22, 27);
constexpr graphics::Color kSelected = graphics::rgb(57, 34, 37);
constexpr graphics::Color kShadow = graphics::rgb(5, 8, 11);
constexpr graphics::Color kMoon = graphics::rgb(54, 61, 67);
constexpr graphics::Color kMountainRear = graphics::rgb(28, 35, 42);
constexpr graphics::Color kMountainFront = graphics::rgb(17, 23, 29);

int32_t minimum(int32_t left, int32_t right) {
    return left < right ? left : right;
}

int32_t maximum(int32_t left, int32_t right) {
    return left > right ? left : right;
}

void line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, graphics::Color color) {
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t dy_abs = y1 > y0 ? y1 - y0 : y0 - y1;
    int32_t dy = -dy_abs;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t error = dx + dy;
    for (;;) {
        graphics::put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        const int32_t twice = error * 2;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

int32_t rounded_inset(int32_t row, int32_t height, int32_t radius) {
    if (radius <= 0) return 0;
    int32_t distance;
    if (row < radius) distance = radius - 1 - row;
    else if (row >= height - radius) distance = row - (height - radius);
    else return 0;
    const int32_t radius_squared = radius * radius;
    int32_t horizontal = radius;
    while (horizontal > 0 &&
           horizontal * horizontal + distance * distance > radius_squared) {
        --horizontal;
    }
    return radius - horizontal;
}

void rounded_fill(const Rect& bounds, int32_t radius, graphics::Color color) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    int32_t r = radius;
    if (r < 0) r = 0;
    if (r * 2 > bounds.width) r = bounds.width / 2;
    if (r * 2 > bounds.height) r = bounds.height / 2;
    if (r == 0) {
        graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, color);
        return;
    }
    for (int32_t row = 0; row < bounds.height; ++row) {
        const int32_t inset = rounded_inset(row, bounds.height, r);
        const int32_t width = bounds.width - inset * 2;
        if (width > 0) {
            graphics::fill_rect(
                bounds.x + inset,
                bounds.y + row,
                width,
                1,
                color);
        }
    }
}

void rounded_surface(
    const Rect& bounds,
    int32_t radius,
    graphics::Color fill,
    graphics::Color border,
    bool shadow) {
    if (shadow) {
        rounded_fill(
            {bounds.x + 4, bounds.y + 5, bounds.width, bounds.height},
            radius,
            kShadow);
    }
    rounded_fill(bounds, radius, border);
    if (bounds.width > 2 && bounds.height > 2) {
        rounded_fill(
            {bounds.x + 1, bounds.y + 1, bounds.width - 2, bounds.height - 2},
            radius > 1 ? radius - 1 : 0,
            fill);
    }
}

void circle_fill(int32_t center_x, int32_t center_y, int32_t radius, graphics::Color color) {
    if (radius <= 0) return;
    const int32_t squared = radius * radius;
    for (int32_t y = -radius; y <= radius; ++y) {
        int32_t extent = radius;
        while (extent > 0 && extent * extent + y * y > squared) --extent;
        graphics::fill_rect(center_x - extent, center_y + y, extent * 2 + 1, 1, color);
    }
}

void circle_ring(
    int32_t center_x,
    int32_t center_y,
    int32_t radius,
    int32_t thickness,
    graphics::Color color) {
    if (radius <= 0 || thickness <= 0) return;
    const int32_t outer = radius * radius;
    const int32_t inner_radius = radius > thickness ? radius - thickness : 0;
    const int32_t inner = inner_radius * inner_radius;
    for (int32_t y = -radius; y <= radius; ++y) {
        for (int32_t x = -radius; x <= radius; ++x) {
            const int32_t distance = x * x + y * y;
            if (distance <= outer && distance >= inner) {
                graphics::put_pixel(center_x + x, center_y + y, color);
            }
        }
    }
}

void vertical_gradient(
    const Rect& bounds,
    graphics::Color top,
    graphics::Color bottom) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const uint32_t tr = (top >> 16U) & 0xFFU;
    const uint32_t tg = (top >> 8U) & 0xFFU;
    const uint32_t tb = top & 0xFFU;
    const uint32_t br = (bottom >> 16U) & 0xFFU;
    const uint32_t bg = (bottom >> 8U) & 0xFFU;
    const uint32_t bb = bottom & 0xFFU;
    const uint32_t denominator = bounds.height > 1
        ? static_cast<uint32_t>(bounds.height - 1)
        : 1U;
    for (int32_t row = 0; row < bounds.height; ++row) {
        const uint32_t step = static_cast<uint32_t>(row);
        const uint8_t red = static_cast<uint8_t>(
            (tr * (denominator - step) + br * step) / denominator);
        const uint8_t green = static_cast<uint8_t>(
            (tg * (denominator - step) + bg * step) / denominator);
        const uint8_t blue = static_cast<uint8_t>(
            (tb * (denominator - step) + bb * step) / denominator);
        graphics::fill_rect(
            bounds.x,
            bounds.y + row,
            bounds.width,
            1,
            graphics::rgb(red, green, blue));
    }
}

void mountain(
    int32_t peak_x,
    int32_t peak_y,
    int32_t base_y,
    int32_t half_width,
    graphics::Color color) {
    const int32_t height = base_y - peak_y;
    if (height <= 0 || half_width <= 0) return;
    for (int32_t row = 0; row < height; ++row) {
        const int32_t width = (row * half_width) / height;
        graphics::fill_rect(
            peak_x - width,
            peak_y + row,
            width * 2 + 1,
            1,
            color);
    }
}

void brand_mark(int32_t x, int32_t y, int32_t size) {
    if (size < 12) return;
    const int32_t center_x = x + size / 2;
    const int32_t center_y = y + size / 2;
    circle_ring(center_x, center_y, size / 2 - 1, 2, kTheme.text_muted);
    line(x + size / 3, y + size / 5, x + size / 3, y + size * 4 / 5, kTheme.text);
    line(x + size / 3, center_y, x + size * 3 / 4, y + size / 4, kTheme.text);
    line(x + size / 3, center_y, x + size * 3 / 4, y + size * 3 / 4, kTheme.text);
    line(x + size / 5, y + size * 4 / 5, x + size * 4 / 5, y + size / 5, kTheme.accent);
}

void wallpaper() {
    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t height = static_cast<int32_t>(graphics::height());
    vertical_gradient({0, 0, width, height}, kDesktopTop, kDesktopBottom);

    const int32_t center_x = width / 2;
    const int32_t horizon = height * 5 / 6;
    const int32_t moon_radius = minimum(width, height) / 7;
    circle_ring(
        center_x - width / 6,
        height / 3,
        moon_radius,
        2,
        kMoon);

    mountain(center_x - width / 5, height / 2, horizon, width / 3, kMountainRear);
    mountain(center_x + width / 5, height * 9 / 20, horizon, width / 3, kMountainRear);
    mountain(center_x, height / 3, horizon, width / 3, kMountainFront);
    mountain(center_x - width / 12, height * 7 / 15, horizon, width / 5, graphics::rgb(12, 18, 23));

    const int32_t beam_top = maximum(24, height / 12);
    const int32_t beam_bottom = height / 3 + 22;
    graphics::fill_rect(center_x - 1, beam_top, 2, beam_bottom - beam_top, kTheme.accent);
    graphics::fill_rect(center_x - 3, beam_bottom - 8, 6, 18, graphics::rgb(113, 30, 30));
    circle_fill(center_x, beam_bottom, 4, kTheme.accent);
}

void draw_icon(const Rect& bounds, DockIcon icon, graphics::Color color) {
    const int32_t cx = bounds.x + bounds.width / 2;
    const int32_t cy = bounds.y + bounds.height / 2;
    switch (icon) {
        case DockIcon::Home:
            brand_mark(cx - 10, cy - 10, 20);
            break;
        case DockIcon::Terminal:
            graphics::draw_rect(cx - 10, cy - 8, 20, 16, color, 1U);
            line(cx - 6, cy - 3, cx - 2, cy, color);
            line(cx - 2, cy, cx - 6, cy + 3, color);
            graphics::fill_rect(cx + 1, cy + 3, 6, 1, color);
            break;
        case DockIcon::Files:
            rounded_fill({cx - 10, cy - 6, 20, 13}, 3, color);
            graphics::fill_rect(cx - 8, cy - 9, 8, 4, color);
            break;
        case DockIcon::Monitor:
            graphics::draw_rect(cx - 10, cy - 8, 20, 14, color, 1U);
            line(cx - 6, cy + 3, cx - 2, cy - 1, color);
            line(cx - 2, cy - 1, cx + 1, cy + 1, color);
            line(cx + 1, cy + 1, cx + 6, cy - 4, color);
            graphics::fill_rect(cx - 4, cy + 9, 8, 1, color);
            break;
        case DockIcon::Settings:
            circle_ring(cx, cy, 8, 2, color);
            circle_fill(cx, cy, 2, color);
            for (int32_t i = -1; i <= 1; i += 2) {
                graphics::fill_rect(cx + i * 9, cy - 2, 2, 4, color);
                graphics::fill_rect(cx - 2, cy + i * 9, 4, 2, color);
            }
            break;
        case DockIcon::About:
            circle_ring(cx, cy, 9, 1, color);
            graphics::fill_rect(cx, cy - 3, 1, 8, color);
            graphics::fill_rect(cx, cy - 7, 1, 1, color);
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
    wallpaper();
    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t height = static_cast<int32_t>(graphics::height());
    const Rect card{width / 2 - 180, height / 2 - 82, 360, 164};
    rounded_surface(card, 18, kTheme.panel, kTheme.border, true);
    brand_mark(card.x + 22, card.y + 24, 46);
    graphics::draw_text(
        card.x + 82, card.y + 28, "KuroganeOS", kTheme.text, kTheme.panel, 2U, true);
    graphics::draw_text(
        card.x + 82, card.y + 52, KUROGANE_VERSION_STRING,
        kTheme.text_muted, kTheme.panel, 1U, true);
    if (stage != nullptr) {
        graphics::draw_text(card.x + 24, card.y + 92, stage, kTheme.text_muted, kTheme.panel, 1U, true);
    }
    progress({card.x + 24, card.y + 122, card.width - 48, 10}, progress_value, 100U);
}

void login_backdrop(const char* status) {
    wallpaper();
    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t height = static_cast<int32_t>(graphics::height());
    const Rect card{width / 2 - 190, height / 2 - 105, 380, 210};
    rounded_surface(card, 20, graphics::rgb(17, 22, 27), kTheme.border, true);
    brand_mark(card.x + card.width / 2 - 25, card.y + 24, 50);
    graphics::draw_text(
        card.x + 104, card.y + 88, "Welcome to KuroganeOS",
        kTheme.text, kTheme.panel, 2U, true);
    if (status != nullptr) {
        graphics::draw_text(
            card.x + 70, card.y + 126, status,
            kTheme.text_muted, kTheme.panel, 1U, true);
    }
    rounded_surface(
        {card.x + 100, card.y + 158, 180, 32},
        10,
        kSelected,
        kTheme.accent,
        false);
    graphics::draw_text(
        card.x + 126, card.y + 168, "ENTER TO CONTINUE",
        kTheme.text, kSelected, 1U, true);
}

void desktop(const char*) {
    wallpaper();
    brand_mark(18, 16, 30);
    graphics::draw_text(58, 25, "KuroganeOS", kTheme.text, kTheme.desktop, 1U, true);
}

void panel(const Rect& bounds, bool raised) {
    rounded_surface(
        bounds,
        12,
        raised ? kTheme.panel_alt : kTheme.panel,
        kTheme.border,
        raised);
}

void window(const Rect& bounds, const char* title) {
    flux_window(bounds, title, true);
}

void flux_window(const Rect& bounds, const char* title, bool focused) {
    const graphics::Color border = focused ? graphics::rgb(70, 77, 84) : kTheme.border;
    rounded_surface(bounds, 14, kTheme.panel, border, true);
    if (bounds.height > 36) {
        rounded_fill({bounds.x + 1, bounds.y + 1, bounds.width - 2, 35}, 13, kSurfaceInset);
        graphics::fill_rect(bounds.x + 12, bounds.y + 35, bounds.width - 24, 1, kTheme.border);
    }
    if (title != nullptr) {
        graphics::draw_text(
            bounds.x + 16,
            bounds.y + 12,
            title,
            focused ? kTheme.text : kTheme.text_muted,
            kSurfaceInset,
            1U,
            true);
    }
}

void flux_control(const Rect& bounds, FluxControl control, bool active) {
    graphics::Color fill = active ? kTheme.panel_alt : kSurfaceInset;
    graphics::Color foreground = active ? kTheme.text : kTheme.text_muted;
    if (control == FluxControl::Dismiss && active) {
        fill = graphics::rgb(69, 28, 30);
        foreground = kTheme.danger;
    }
    rounded_surface(bounds, 7, fill, active ? foreground : kTheme.border, false);
    const int32_t cx = bounds.x + bounds.width / 2;
    const int32_t cy = bounds.y + bounds.height / 2;
    if (control == FluxControl::Minimize) {
        graphics::fill_rect(cx - 4, cy + 2, 8, 1, foreground);
    } else if (control == FluxControl::Expand) {
        graphics::draw_rect(cx - 4, cy - 4, 8, 8, foreground, 1U);
    } else {
        line(cx - 4, cy - 4, cx + 4, cy + 4, foreground);
        line(cx + 4, cy - 4, cx - 4, cy + 4, foreground);
    }
}

void signal_spine(const Rect&, size_t, size_t) {
    /* Obsidian deliberately removes the Red Flux left rail. */
}

void dock_bar(const Rect& bounds, size_t) {
    rounded_surface(bounds, 17, graphics::rgb(18, 23, 28), graphics::rgb(61, 68, 75), true);
}

void dock_item(const Rect& bounds, DockIcon icon, bool running, bool focused) {
    const graphics::Color fill = focused ? kSelected : graphics::rgb(22, 27, 32);
    const graphics::Color border = focused ? kTheme.accent : graphics::rgb(45, 52, 58);
    rounded_surface(bounds, 11, fill, border, false);
    draw_icon(bounds, icon, focused ? kTheme.text : graphics::rgb(199, 205, 211));
    if (running) {
        const int32_t width = focused ? 16 : 8;
        graphics::fill_rect(
            bounds.x + (bounds.width - width) / 2,
            bounds.y + bounds.height - 3,
            width,
            2,
            kTheme.accent);
    }
}

void dock_task(const Rect& bounds, const char* title, bool focused, bool minimized) {
    graphics::Color fill = focused ? kSelected : graphics::rgb(22, 27, 32);
    graphics::Color foreground = minimized ? kTheme.text_muted : kTheme.text;
    rounded_surface(bounds, 9, fill, focused ? kTheme.accent : kTheme.border, false);
    if (title != nullptr) {
        graphics::draw_text(
            bounds.x + 10,
            bounds.y + (bounds.height - 7) / 2,
            title,
            foreground,
            fill,
            1U,
            true);
    }
}

void pulse_ribbon(const Rect& bounds, size_t window_count) {
    dock_bar(bounds, window_count);
}

void pulse_item(const Rect& bounds, const char* title, bool focused, bool minimized) {
    dock_task(bounds, title, focused, minimized);
}

void label(const Rect& bounds, const char* text, graphics::Color color, uint32_t scale) {
    if (text == nullptr) return;
    graphics::draw_text(
        bounds.x,
        bounds.y,
        text,
        color == 0U ? kTheme.text : color,
        kTheme.panel,
        scale,
        true);
}

void button(const Rect& bounds, const char* text, bool selected) {
    const graphics::Color fill = selected ? kSelected : graphics::rgb(22, 27, 32);
    rounded_surface(
        bounds,
        10,
        fill,
        selected ? kTheme.accent : kTheme.border,
        false);
    if (text != nullptr) {
        const size_t length = [] (const char* value) {
            size_t count = 0U;
            if (value == nullptr) return count;
            while (value[count] != '\0' && count < 32U) ++count;
            return count;
        }(text);
        const int32_t text_width = static_cast<int32_t>(length * 6U);
        graphics::draw_text(
            bounds.x + maximum(8, (bounds.width - text_width) / 2),
            bounds.y + (bounds.height - 7) / 2,
            text,
            selected ? kTheme.text : graphics::rgb(205, 211, 216),
            fill,
            1U,
            true);
    }
}

void progress(const Rect& bounds, uint32_t value, uint32_t maximum_value) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    rounded_fill(bounds, bounds.height / 2, graphics::rgb(38, 45, 51));
    if (maximum_value == 0U) return;
    if (value > maximum_value) value = maximum_value;
    const int32_t inner_width = bounds.width > 4 ? bounds.width - 4 : bounds.width;
    const int32_t fill_width = static_cast<int32_t>(
        (static_cast<uint64_t>(inner_width) * value) / maximum_value);
    if (fill_width > 0 && bounds.height > 4) {
        rounded_fill(
            {bounds.x + 2, bounds.y + 2, fill_width, bounds.height - 4},
            (bounds.height - 4) / 2,
            kTheme.accent);
    }
}

void separator(int32_t x, int32_t y, int32_t width) {
    if (width <= 0) return;
    graphics::fill_rect(x, y, width, 1, kTheme.border);
}

void taskbar(const char* status) {
    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t height = static_cast<int32_t>(graphics::height());
    const Rect bar{width / 2 - 220, height - 54, 440, 42};
    rounded_surface(bar, 14, kTheme.panel, kTheme.border, true);
    if (status != nullptr) {
        graphics::draw_text(
            bar.x + 16,
            bar.y + 17,
            status,
            kTheme.text_muted,
            kTheme.panel,
            1U,
            true);
    }
}

} // namespace ui

#include "ui.hpp"

#include "font.hpp"
#include "forged_surface.hpp"
#include "icon_registry.hpp"

#include "../../common/version.h"

namespace ui {
namespace {

// Approved KuroganeOS 5 / Forged Steel design tokens.
constexpr graphics::Color kObsidian = graphics::rgb(9, 14, 14);       // #090E0E
constexpr graphics::Color kSteel = graphics::rgb(23, 28, 34);        // #171C22
constexpr graphics::Color kSteelDeep = graphics::rgb(12, 17, 21);
constexpr graphics::Color kSteelRaised = graphics::rgb(31, 37, 44);
constexpr graphics::Color kSteelEdge = graphics::rgb(52, 59, 67);
constexpr graphics::Color kSteelHairline = graphics::rgb(76, 83, 91);
constexpr graphics::Color kAsh = graphics::rgb(168, 175, 184);       // #A8AFB8
constexpr graphics::Color kText = graphics::rgb(238, 241, 244);
constexpr graphics::Color kMuted = graphics::rgb(125, 134, 144);
constexpr graphics::Color kCrimson = graphics::rgb(230, 41, 50);     // #E62932
constexpr graphics::Color kHotEdge = graphics::rgb(255, 74, 69);     // #FF4A45
constexpr graphics::Color kGlowDeep = graphics::rgb(91, 19, 25);
constexpr graphics::Color kShadow = graphics::rgb(2, 4, 5);

constexpr Theme kTheme = {
    kObsidian,
    kSteel,
    kSteelDeep,
    kSteelEdge,
    kText,
    kAsh,
    kCrimson,
    kHotEdge,
};

int32_t absolute(int32_t value) { return value < 0 ? -value : value; }

void line(
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    graphics::Color color) {
    const int32_t dx = absolute(x1 - x0);
    const int32_t sx = x0 < x1 ? 1 : -1;
    const int32_t dy = -absolute(y1 - y0);
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

void fill_chamfered(
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    int32_t cut,
    graphics::Color color) {
    if (width <= 0 || height <= 0) return;
    if (cut < 0) cut = 0;
    if (cut * 2 > height) cut = height / 2;
    for (int32_t row = 0; row < height; ++row) {
        int32_t inset = 0;
        if (row < cut) inset = cut - row;
        else if (row >= height - cut) inset = row - (height - cut - 1);
        if (inset * 2 >= width) continue;
        graphics::fill_rect(x + inset, y + row, width - inset * 2, 1, color);
    }
}

void outline_chamfered(
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    int32_t cut,
    graphics::Color edge,
    graphics::Color fill) {
    fill_chamfered(x + 4, y + 5, width, height, cut, kShadow);
    fill_chamfered(x, y, width, height, cut, edge);
    fill_chamfered(x + 1, y + 1, width - 2, height - 2,
                   cut > 0 ? cut - 1 : 0, fill);
}

void draw_brand(int32_t x, int32_t y, int32_t size) {
    if (icons::valid(KU_ICON_BRANDING_LOGO_MAIN)) {
        icons::draw(KU_ICON_BRANDING_LOGO_MAIN, x, y, size, size);
        return;
    }
    const int32_t cx = x + size / 2;
    const int32_t cy = y + size / 2;
    const int32_t radius = size / 2 - 2;
    line(cx - radius, cy + radius, cx - radius / 2, cy - radius, kAsh);
    line(cx - radius / 2, cy - radius, cx - 2, cy - radius / 3, kSteelHairline);
    line(cx - 2, cy - radius / 3, cx + radius, cy - radius, kHotEdge);
    line(cx - 2, cy, cx + radius, cy + radius, kCrimson);
}

void draw_brushed_backdrop() {
    if (!graphics::available()) return;
    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t height = static_cast<int32_t>(graphics::height());
    graphics::clear(kObsidian);

    // Sparse texture rather than per-pixel noise: visually closer to brushed
    // forged steel while keeping QEMU/TCG frame cost bounded.
    for (int32_t y = 8; y < height; y += 28) {
        graphics::fill_rect(0, y, width, 1, graphics::rgb(13, 19, 22));
    }
    for (int32_t y = 18; y < height; y += 84) {
        graphics::fill_rect(width / 5, y, width * 3 / 5, 1,
                            graphics::rgb(18, 23, 27));
    }

    // Energy seam from the design board. It is intentionally geometric and
    // sparse so it does not become an animation/performance tax.
    if (width > 760 && height > 480) {
        int32_t x = width / 2 + 40;
        int32_t y = 58;
        const int32_t end_y = height - 100;
        while (y < end_y) {
            const int32_t next_y = y + 48;
            const int32_t next_x = x + ((y / 48) % 2 == 0 ? -28 : 18);
            line(x, y, next_x, next_y, kGlowDeep);
            line(x + 1, y, next_x + 1, next_y, kCrimson);
            if ((y / 48) % 3 == 0) {
                line(x + 3, y + 4, next_x + 3, next_y - 3, kHotEdge);
            }
            x = next_x;
            y = next_y;
        }
    }
}

void draw_top_identity(const char* title) {
    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t rail_height = 52;
    graphics::fill_rect(0, 0, width, rail_height, graphics::rgb(5, 9, 11));
    graphics::fill_rect(0, rail_height - 1, width, 1, kSteelEdge);
    graphics::fill_rect(0, rail_height - 2, width / 3, 1, kGlowDeep);

    draw_brand(22, 9, 34);
    font::draw(font::Face::Display, 68, 13,
               title ? title : "KUROGANEOS 5.0",
               kText, graphics::rgb(5, 9, 11), 2U, true);
    font::draw(font::Face::Ui, 70, 34,
               "KUROGANE FORGE DESKTOP ENVIRONMENT",
               kMuted, graphics::rgb(5, 9, 11), 1U, true);

    if (width > 850) {
        const char* tagline = "BUILT IN STEEL. REFINED IN FIRE.";
        const int32_t text_width = font::measure(font::Face::Ui, tagline, 1U);
        font::draw(font::Face::Ui, (width - text_width) / 2, 21,
                   tagline, kAsh, graphics::rgb(5, 9, 11), 1U, true);
        graphics::fill_rect(width / 2 + text_width / 2 + 9, 19, 24, 1, kCrimson);
    }

    if (width > 1120) {
        font::draw(font::Face::Ui, width - 224, 20,
                   "FORGED STEEL / DEV BETA",
                   kMuted, graphics::rgb(5, 9, 11), 1U, true);
    }
}

void control_minimize(const Rect& bounds, graphics::Color color) {
    graphics::fill_rect(bounds.x + 6, bounds.y + bounds.height - 7,
                        bounds.width - 12, 1, color);
}

void control_expand(const Rect& bounds, graphics::Color color) {
    const int32_t left = bounds.x + 6;
    const int32_t top = bounds.y + 5;
    const int32_t right = bounds.x + bounds.width - 7;
    const int32_t bottom = bounds.y + bounds.height - 6;
    graphics::fill_rect(left, top, 7, 1, color);
    graphics::fill_rect(left, top, 1, 7, color);
    graphics::fill_rect(right - 6, bottom, 7, 1, color);
    graphics::fill_rect(right, bottom - 6, 1, 7, color);
}

void control_dismiss(const Rect& bounds, graphics::Color color) {
    line(bounds.x + 7, bounds.y + 5,
         bounds.x + bounds.width - 8, bounds.y + bounds.height - 6, color);
    line(bounds.x + bounds.width - 8, bounds.y + 5,
         bounds.x + 7, bounds.y + bounds.height - 6, color);
}

void draw_dock_icon(
    const Rect& bounds,
    DockIcon icon,
    bool active) {
    ku_icon_id_t asset = KU_ICON_NONE;
    switch (icon) {
        case DockIcon::Home: asset = KU_ICON_KUROGANE_APP_BLADE_LAUNCHER; break;
        case DockIcon::Terminal: asset = KU_ICON_KUROGANE_APP_KUROSH_TERMINAL; break;
        case DockIcon::Files: asset = KU_ICON_KUROGANE_APP_VAULT_FILE_MANAGER; break;
        case DockIcon::Performance: asset = KU_ICON_SPECIAL_CPU; break;
        case DockIcon::Web: asset = KU_ICON_APPLICATION_BROWSER; break;
        case DockIcon::SystemMonitor: asset = KU_ICON_APPLICATION_SYSTEM_MONITOR; break;
        case DockIcon::Settings: asset = KU_ICON_KUROGANE_APP_FORGE_CONTROL; break;
        case DockIcon::About: asset = KU_ICON_STATUS_INFO; break;
        case DockIcon::Anvil: asset = KU_ICON_KUROGANE_APP_ANVIL_PACKAGE_MANAGER; break;
        case DockIcon::Pulse: asset = KU_ICON_KUROGANE_APP_PULSE_QUICK_SETTINGS; break;
    }
    if (asset != KU_ICON_NONE && icons::valid(asset)) {
        const int32_t size = bounds.height > 38 ? 28 : 22;
        icons::draw(asset,
                    bounds.x + (bounds.width - size) / 2,
                    bounds.y + (bounds.height - size) / 2 - (active ? 1 : 0),
                    size, size);
    } else {
        draw_brand(bounds.x + bounds.width / 2 - 11,
                   bounds.y + bounds.height / 2 - 11, 22);
    }
}

void copy_short_label(char* destination, size_t capacity, const char* source) {
    if (destination == nullptr || capacity == 0U) return;
    size_t written = 0U;
    while (source != nullptr && source[written] != '\0' &&
           written + 1U < capacity) {
        destination[written] = source[written];
        ++written;
    }
    destination[written] = '\0';
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
    draw_brushed_backdrop();

    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t height = static_cast<int32_t>(graphics::height());
    const int32_t cx = width / 2;
    const int32_t cy = height / 2;

    draw_brand(cx - 52, cy - 142, 104);
    font::draw(font::Face::Display, cx - 145, cy - 12,
               "KUROGANEOS", kText, kObsidian, 3U, true);
    font::draw(font::Face::Ui, cx - 92, cy + 28,
               "BUILT IN STEEL. REFINED IN FIRE.",
               kAsh, kObsidian, 1U, true);
    font::draw(font::Face::Mono, cx - 106, cy + 54,
               stage ? stage : "INITIALIZING FORGED STEEL",
               kMuted, kObsidian, 1U, true);

    if (progress_value > 100U) progress_value = 100U;
    const int32_t bar_width = width > 620 ? 420 : width - 100;
    const int32_t bar_x = cx - bar_width / 2;
    const int32_t bar_y = cy + 86;
    graphics::fill_rect(bar_x, bar_y, bar_width, 5, kSteel);
    const int32_t active = static_cast<int32_t>(
        (static_cast<uint64_t>(bar_width) * progress_value) / 100U);
    if (active > 0) graphics::fill_rect(bar_x, bar_y, active, 5, kCrimson);
    graphics::fill_rect(bar_x, bar_y, active > 58 ? 58 : active, 1, kHotEdge);
}

void login_backdrop(const char* status) {
    if (!graphics::available()) return;
    graphics::reset_clip();
    graphics::reset_text_scale_limit();
    draw_brushed_backdrop();
    draw_top_identity("KUROGANEOS 5.0");

    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t height = static_cast<int32_t>(graphics::height());
    const int32_t cx = width / 2;
    draw_brand(cx - 45, 108, 90);
    font::draw(font::Face::Display, cx - 104, 218,
               "SECURE ACCESS", kText, kObsidian, 2U, true);
    font::draw(font::Face::Ui, cx - 126, 246,
               "LOCAL FORGED STEEL SESSION GATE",
               kAsh, kObsidian, 1U, true);
    graphics::fill_rect(cx - 150, 272, 300, 1, kSteelEdge);
    graphics::fill_rect(cx - 150, 272, 82, 2, kCrimson);
    if (status != nullptr) {
        font::draw(font::Face::Mono, cx - 120, height - 48,
                   status, kMuted, kObsidian, 1U, true);
    }
}

void desktop(const char* title) {
    if (!graphics::available()) return;
    graphics::reset_clip();
    graphics::reset_text_scale_limit();
    draw_brushed_backdrop();
    draw_top_identity(title ? title : "KUROGANEOS 5.0");

    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t height = static_cast<int32_t>(graphics::height());

    // Low-contrast forge mark below app surfaces.
    if (width > 760 && height > 520) {
        draw_brand(width / 2 - 72, height / 2 - 84, 144);
        graphics::fill_rect(width / 2 - 122, height / 2 + 76, 244, 1,
                            graphics::rgb(31, 29, 33));
        graphics::fill_rect(width / 2 - 42, height / 2 + 76, 84, 2,
                            kGlowDeep);
    }
}

void panel(const Rect& bounds, bool raised) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    outline_chamfered(bounds.x, bounds.y, bounds.width, bounds.height,
                      7, raised ? kSteelHairline : kSteelEdge,
                      raised ? kSteelRaised : kSteelDeep);
    graphics::fill_rect(bounds.x + 12, bounds.y + 1,
                        bounds.width > 84 ? 72 : bounds.width - 24,
                        1, raised ? kHotEdge : kGlowDeep);
}

void flux_window(const Rect& bounds, const char* title, bool focused) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const graphics::Color edge = focused ? kHotEdge : kSteelEdge;
    const graphics::Color fill = focused ? graphics::rgb(18, 24, 28) : kSteelDeep;
    outline_chamfered(bounds.x, bounds.y, bounds.width, bounds.height,
                      8, edge, fill);

    // Forged top lip and hot-edge signature.
    graphics::fill_rect(bounds.x + 18, bounds.y + 1,
                        bounds.width > 190 ? 154 : bounds.width - 36,
                        2, focused ? kCrimson : kSteelEdge);
    graphics::fill_rect(bounds.x + 9, bounds.y + 9, 2, 17,
                        focused ? kHotEdge : kCrimson);
    graphics::fill_rect(bounds.x + 12, bounds.y + 34,
                        bounds.width - 24, 1, kSteelEdge);
    graphics::fill_rect(bounds.x + 12, bounds.y + 34,
                        bounds.width > 152 ? 128 : bounds.width - 24,
                        1, focused ? kCrimson : kGlowDeep);

    font::draw(font::Face::Display,
               bounds.x + 24, bounds.y + 11,
               title ? title : "SURFACE",
               focused ? kText : kAsh,
               fill, 1U, true);

    // Bottom-right forged corner grip.
    const graphics::Color grip = focused ? kCrimson : kSteelHairline;
    line(bounds.x + bounds.width - 20, bounds.y + bounds.height - 3,
         bounds.x + bounds.width - 3, bounds.y + bounds.height - 20, grip);
    line(bounds.x + bounds.width - 13, bounds.y + bounds.height - 3,
         bounds.x + bounds.width - 3, bounds.y + bounds.height - 13, grip);
}

void window(const Rect& bounds, const char* title) {
    flux_window(bounds, title, false);
}

void flux_control(const Rect& bounds, FluxControl control, bool active) {
    if (bounds.width <= 8 || bounds.height <= 8) return;
    const graphics::Color fill = active ? graphics::rgb(44, 27, 31) : kSteelDeep;
    fill_chamfered(bounds.x, bounds.y, bounds.width, bounds.height, 4,
                   active ? kHotEdge : kSteelEdge);
    fill_chamfered(bounds.x + 1, bounds.y + 1,
                   bounds.width - 2, bounds.height - 2, 3, fill);
    const graphics::Color glyph = active ? kHotEdge : kAsh;
    switch (control) {
        case FluxControl::Minimize: control_minimize(bounds, glyph); break;
        case FluxControl::Expand: control_expand(bounds, glyph); break;
        case FluxControl::Dismiss: control_dismiss(bounds, active ? kHotEdge : kCrimson); break;
    }
}

void signal_spine(const Rect& bounds, size_t window_count, size_t focused_position) {
    if (bounds.height <= 0) return;

    // The WindowManager ABI still supplies the historic narrow signal rect;
    // the 5.0 renderer intentionally expands inside the reserved left gutter to
    // produce the forged blade shown in the design board.
    const int32_t x = bounds.x - 2;
    const int32_t width = 34;
    const int32_t y = bounds.y - 4;
    const int32_t height = bounds.height + 8;

    fill_chamfered(x + 3, y + 5, width, height, 8, kShadow);
    fill_chamfered(x, y, width, height, 8, kSteelHairline);
    fill_chamfered(x + 1, y + 1, width - 2, height - 2, 7, kSteelDeep);
    graphics::fill_rect(x + 6, y + 20, 2, height - 40, kSteelEdge);
    graphics::fill_rect(x + width - 9, y + 36, 2, height - 72, kGlowDeep);
    graphics::fill_rect(x + width - 8, y + height / 3,
                        1, height / 3, kCrimson);

    const size_t visible = window_count < 9U ? window_count : 9U;
    const size_t slots = visible == 0U ? 4U : visible;
    const int32_t usable = height - 90;
    const int32_t step = slots > 1U
        ? usable / static_cast<int32_t>(slots - 1U) : 0;
    for (size_t index = 0U; index < slots; ++index) {
        const int32_t node_y = y + 45 + static_cast<int32_t>(index) * step;
        const bool selected = visible != 0U && index == focused_position;
        const graphics::Color edge = selected ? kHotEdge : kSteelEdge;
        fill_chamfered(x + 8, node_y - 8, 18, 18, 4, edge);
        fill_chamfered(x + 9, node_y - 7, 16, 16, 3,
                       selected ? graphics::rgb(45, 25, 30) : kSteel);
        if (selected) graphics::fill_rect(x + 22, node_y - 4, 3, 10, kCrimson);
    }

    draw_brand(x + 3, y + height / 2 - 14, 28);
}

void dock_bar(const Rect& bounds, size_t running_count) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const graphics::Color edge = running_count == 0U ? kSteelEdge : kSteelHairline;
    outline_chamfered(bounds.x, bounds.y, bounds.width, bounds.height,
                      9, edge, graphics::rgb(8, 13, 16));
    graphics::fill_rect(bounds.x + 20, bounds.y + 1,
                        bounds.width > 130 ? 96 : bounds.width - 40,
                        1, kCrimson);
    graphics::fill_rect(bounds.x + bounds.width - 80, bounds.y + 1,
                        48, 1, running_count == 0U ? kGlowDeep : kHotEdge);
}

void dock_item(const Rect& bounds, DockIcon icon, bool running, bool focused) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const graphics::Color edge = focused ? kHotEdge : (running ? kSteelHairline : kSteelEdge);
    const graphics::Color fill = focused ? graphics::rgb(46, 25, 30) : kSteelDeep;
    fill_chamfered(bounds.x, bounds.y, bounds.width, bounds.height, 6, edge);
    fill_chamfered(bounds.x + 1, bounds.y + 1,
                   bounds.width - 2, bounds.height - 2, 5, fill);
    if (focused) {
        graphics::fill_rect(bounds.x + 8, bounds.y + 1,
                            bounds.width - 16, 2, kHotEdge);
    }
    draw_dock_icon(bounds, icon, focused || running);
    if (running) {
        graphics::fill_rect(bounds.x + bounds.width / 2 - 4,
                            bounds.y + bounds.height - 5,
                            8, 2, focused ? kHotEdge : kCrimson);
    }
}

void dock_task(const Rect& bounds, const char* title, bool focused, bool minimized) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const graphics::Color fill = minimized ? graphics::rgb(8, 12, 15) : kSteelDeep;
    const graphics::Color edge = focused ? kHotEdge : kSteelEdge;
    fill_chamfered(bounds.x, bounds.y, bounds.width, bounds.height, 5, edge);
    fill_chamfered(bounds.x + 1, bounds.y + 1,
                   bounds.width - 2, bounds.height - 2, 4, fill);
    graphics::fill_rect(bounds.x + 4, bounds.y + 6,
                        focused ? 3 : 1, bounds.height - 12,
                        focused ? kHotEdge : kCrimson);
    char label[15];
    copy_short_label(label, sizeof(label), title ? title : "APP");
    font::draw(font::Face::Ui, bounds.x + 11, bounds.y + 10,
               label, minimized ? kMuted : kAsh, fill, 1U, true);
}

void pulse_ribbon(const Rect& bounds, size_t window_count) {
    dock_bar(bounds, window_count);
}

void pulse_item(const Rect& bounds, const char* title, bool focused, bool minimized) {
    dock_task(bounds, title, focused, minimized);
}

void label(
    const Rect& bounds,
    const char* text,
    graphics::Color color,
    uint32_t scale) {
    font::draw(font::Face::Ui, bounds.x, bounds.y,
               text ? text : "",
               color == 0U ? kText : color,
               kSteel, scale, true);
}

void button(const Rect& bounds, const char* text, bool selected) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const graphics::Color fill = selected ? graphics::rgb(44, 27, 32) : kSteel;
    const graphics::Color edge = selected ? kHotEdge : kSteelEdge;
    fill_chamfered(bounds.x, bounds.y, bounds.width, bounds.height, 5, edge);
    fill_chamfered(bounds.x + 1, bounds.y + 1,
                   bounds.width - 2, bounds.height - 2, 4, fill);
    graphics::fill_rect(bounds.x + 5, bounds.y + 6,
                        selected ? 3 : 1, bounds.height - 12,
                        selected ? kHotEdge : kCrimson);
    font::draw(font::Face::Ui, bounds.x + 12,
               bounds.y + (bounds.height - 8) / 2 - 1,
               text ? text : "",
               selected ? kText : kAsh,
               fill, 1U, true);
}

void progress(const Rect& bounds, uint32_t value, uint32_t maximum) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    fill_chamfered(bounds.x, bounds.y, bounds.width, bounds.height,
                   4, kSteelEdge);
    fill_chamfered(bounds.x + 1, bounds.y + 1,
                   bounds.width - 2, bounds.height - 2, 3, kSteelDeep);
    if (maximum == 0U || bounds.width <= 12) return;
    if (value > maximum) value = maximum;
    const int32_t x = bounds.x + 6;
    const int32_t y = bounds.y + bounds.height / 2 - 3;
    const int32_t width = bounds.width - 12;
    graphics::fill_rect(x, y, width, 6, kSteel);
    const int32_t active = static_cast<int32_t>(
        (static_cast<uint64_t>(width) * value) / maximum);
    graphics::fill_rect(x, y, active, 6, kCrimson);
    graphics::fill_rect(x, y, active > 36 ? 36 : active, 1, kHotEdge);
}

void separator(int32_t x, int32_t y, int32_t width) {
    if (width <= 0) return;
    graphics::fill_rect(x, y, width, 1, kSteelEdge);
    graphics::fill_rect(x, y, width > 72 ? 72 : width, 1, kCrimson);
}

void native_surface(const Rect& bounds, const ku_ui_surface& surface, bool focused) {
    // One canonical native renderer. Keeping the compatibility symbol here
    // prevents older diagnostic callers from falling back to the old Flux UI.
    forged_surface(bounds, surface, focused);
}

void taskbar(const char* status) {
    if (!graphics::available()) return;
    const int32_t width = static_cast<int32_t>(graphics::width());
    const int32_t height = static_cast<int32_t>(graphics::height());
    const Rect bar{18, height - 36, width - 36, 24};
    fill_chamfered(bar.x, bar.y, bar.width, bar.height, 5, kSteelEdge);
    fill_chamfered(bar.x + 1, bar.y + 1,
                   bar.width - 2, bar.height - 2, 4, kSteelDeep);
    font::draw(font::Face::Mono, bar.x + 12, bar.y + 8,
               status ? status : "KUROGANEOS",
               kAsh, kSteelDeep, 1U, true);
}

} // namespace ui

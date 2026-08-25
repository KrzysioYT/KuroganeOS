#include "forged_surface.hpp"

#include "font.hpp"
#include "icon_registry.hpp"

namespace ui {
namespace {

// KuroganeOS 5 design tokens. Keep these values in sync with the approved
// Forged Steel reference instead of drifting toward the older Flux palette.
constexpr graphics::Color kObsidian = graphics::rgb(9, 14, 14);       // #090E0E
constexpr graphics::Color kSteel = graphics::rgb(23, 28, 34);        // #171C22
constexpr graphics::Color kSteelRaised = graphics::rgb(31, 37, 44);
constexpr graphics::Color kSteelActive = graphics::rgb(44, 31, 36);
constexpr graphics::Color kSteelDeep = graphics::rgb(13, 18, 22);
constexpr graphics::Color kSteelEdge = graphics::rgb(52, 59, 67);
constexpr graphics::Color kSteelHairline = graphics::rgb(72, 79, 87);
constexpr graphics::Color kAsh = graphics::rgb(168, 175, 184);       // #A8AFB8
constexpr graphics::Color kText = graphics::rgb(238, 241, 244);
constexpr graphics::Color kMuted = graphics::rgb(128, 137, 147);
constexpr graphics::Color kCrimson = graphics::rgb(230, 41, 50);     // #E62932
constexpr graphics::Color kHotEdge = graphics::rgb(255, 74, 69);     // #FF4A45
constexpr graphics::Color kGlowDeep = graphics::rgb(94, 20, 26);
constexpr graphics::Color kShadow = graphics::rgb(2, 5, 6);

bool starts_with(const char* text, const char* prefix) {
    if (text == nullptr || prefix == nullptr) return false;
    while (*prefix != '\0') {
        if (*text == '\0' || *text != *prefix) return false;
        ++text;
        ++prefix;
    }
    return true;
}

int32_t row_height(uint32_t type) {
    switch (type) {
        case KU_UI_WIDGET_PANEL: return 38;
        case KU_UI_WIDGET_LABEL: return 26;
        case KU_UI_WIDGET_BUTTON:
        case KU_UI_WIDGET_INPUT:
        case KU_UI_WIDGET_LIST_ITEM: return 36;
        case KU_UI_WIDGET_PROGRESS: return 50;
        case KU_UI_WIDGET_SEPARATOR: return 12;
        default: return 26;
    }
}

uint32_t widget_depth(const ku_ui_surface& surface, const ku_ui_widget& widget) {
    uint32_t depth = 0U;
    uint32_t parent = widget.parent_id;
    while (parent != 0U && depth < KU_UI_MAX_WIDGETS) {
        bool found = false;
        for (uint32_t index = 0U; index < surface.widget_count; ++index) {
            if (surface.widgets[index].id != parent) continue;
            parent = surface.widgets[index].parent_id;
            ++depth;
            found = true;
            break;
        }
        if (!found) break;
    }
    return depth;
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
    fill_chamfered(x, y, width, height, cut, edge);
    fill_chamfered(x + 1, y + 1, width - 2, height - 2,
                   cut > 0 ? cut - 1 : 0, fill);
}

// Blade cards deliberately use an asymmetric cut. The reference launcher is a
// stack of forged wedges, not a list of rounded/rectangular desktop buttons.
void fill_blade_card(
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    graphics::Color color) {
    if (width <= 20 || height <= 4) return;
    for (int32_t row = 0; row < height; ++row) {
        const int32_t left_inset = row < 5 ? 5 - row : 0;
        int32_t right_inset = row / 3;
        if (right_inset > 13) right_inset = 13;
        const int32_t line_width = width - left_inset - right_inset;
        if (line_width > 0) {
            graphics::fill_rect(x + left_inset, y + row, line_width, 1, color);
        }
    }
}

void outline_blade_card(
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    graphics::Color edge,
    graphics::Color fill) {
    fill_blade_card(x + 3, y + 4, width, height, kShadow);
    fill_blade_card(x, y, width, height, edge);
    fill_blade_card(x + 1, y + 1, width - 2, height - 2, fill);
}

void draw_icon(const ku_ui_widget& widget, int32_t x, int32_t y, int32_t size) {
    const auto icon = static_cast<ku_icon_id_t>(widget.icon_id);
    if (icon == KU_ICON_NONE || !icons::valid(icon)) return;
    icons::draw(icon, x, y, size, size);
}

bool surface_has_panel_prefix(const ku_ui_surface& surface, const char* prefix) {
    for (uint32_t index = 0U; index < surface.widget_count; ++index) {
        const ku_ui_widget& widget = surface.widgets[index];
        if ((widget.flags & KU_UI_WIDGET_HIDDEN) != 0U) continue;
        if (widget.type != KU_UI_WIDGET_PANEL) continue;
        return starts_with(widget.text, prefix);
    }
    return false;
}

bool is_kurosh_surface(const ku_ui_surface& surface) {
    return surface_has_panel_prefix(surface, "KUROSH");
}

bool is_blade_surface(const ku_ui_surface& surface) {
    return surface_has_panel_prefix(surface, "BLADE LAUNCHER");
}

bool is_access_surface(const ku_ui_surface& surface) {
    return surface_has_panel_prefix(surface, "SECURE ACCESS") ||
        surface_has_panel_prefix(surface, "BEZPIECZNY DOSTEP");
}

bool is_forge_surface(const ku_ui_surface& surface) {
    return surface_has_panel_prefix(surface, "FORGE CONTROL");
}

bool is_pulse_surface(const ku_ui_surface& surface) {
    return surface_has_panel_prefix(surface, "PULSE");
}

bool is_anvil_surface(const ku_ui_surface& surface) {
    return surface_has_panel_prefix(surface, "ANVIL");
}

font::Face text_face(bool kurosh, const ku_ui_widget& widget) {
    if (widget.type == KU_UI_WIDGET_PANEL) return font::Face::Display;
    if (kurosh && widget.type != KU_UI_WIDGET_BUTTON) return font::Face::Mono;
    return font::Face::Ui;
}

void draw_micro_etch(const Rect& bounds) {
    if (bounds.width < 180 || bounds.height < 100) return;
    // Low-cost brushed metal cue. It stays sparse so the software renderer does
    // not spend frame time drawing decorative noise.
    for (int32_t y = bounds.y + 50; y < bounds.y + bounds.height; y += 72) {
        graphics::fill_rect(bounds.x + 18, y, bounds.width - 36, 1,
                            graphics::rgb(16, 22, 26));
    }
    for (int32_t y = bounds.y + 74; y < bounds.y + bounds.height; y += 144) {
        graphics::fill_rect(bounds.x + bounds.width - 62, y, 36, 1,
                            graphics::rgb(55, 29, 33));
        graphics::fill_rect(bounds.x + bounds.width - 29, y - 4, 1, 5,
                            graphics::rgb(91, 27, 32));
    }
}

void draw_surface_frame(const Rect& bounds, bool focused, bool access) {
    if (access) {
        fill_chamfered(bounds.x + 5, bounds.y + 6,
                       bounds.width - 5, bounds.height - 6, 9, kShadow);
        outline_chamfered(bounds.x, bounds.y, bounds.width, bounds.height,
                          9, focused ? kHotEdge : kSteelEdge, kObsidian);
        graphics::fill_rect(bounds.x + 18, bounds.y + 1,
                            bounds.width > 230 ? 184 : bounds.width - 36,
                            2, kCrimson);
        return;
    }

    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, kObsidian);
    graphics::fill_rect(bounds.x, bounds.y, 2, bounds.height,
                        focused ? kHotEdge : kSteelEdge);
    graphics::fill_rect(bounds.x + 2, bounds.y, 94, 1,
                        focused ? kCrimson : kSteelEdge);
    graphics::fill_rect(bounds.x + bounds.width - 48, bounds.y + 9, 34, 1,
                        kGlowDeep);
}

void draw_blade_surface(
    const Rect& bounds,
    const ku_ui_surface& surface,
    bool focused) {
    draw_surface_frame(bounds, focused, false);

    // Internal launcher rail. Together with the system spine this produces the
    // twin-blade silhouette from the 5.0 design board without requiring bitmap
    // wallpaper assets or expensive alpha composition.
    const int32_t rail_x = bounds.x + 12;
    graphics::fill_rect(rail_x, bounds.y + 16, 12, bounds.height - 32, kSteelDeep);
    graphics::fill_rect(rail_x, bounds.y + 16, 1, bounds.height - 32, kSteelHairline);
    graphics::fill_rect(rail_x + 9, bounds.y + 28, 2, bounds.height - 56, kGlowDeep);
    graphics::fill_rect(rail_x + 10, bounds.y + 72, 1,
                        bounds.height > 170 ? bounds.height - 170 : 1, kCrimson);

    int32_t y = bounds.y + 10;
    uint32_t visible_index = 0U;
    uint32_t rendered = 0U;
    uint32_t blade_index = 0U;
    const uint32_t row_limit = surface.visible_rows == 0U
        ? KU_UI_MAX_WIDGETS : surface.visible_rows;

    for (uint32_t index = 0U; index < surface.widget_count; ++index) {
        const ku_ui_widget& widget = surface.widgets[index];
        if ((widget.flags & KU_UI_WIDGET_HIDDEN) != 0U) continue;
        if (visible_index++ < surface.scroll_offset) continue;
        if (rendered++ >= row_limit) break;
        const int32_t height = row_height(widget.type);
        if (y + height > bounds.y + bounds.height) break;

        const bool selected =
            (widget.flags & KU_UI_WIDGET_SELECTED) != 0U ||
            surface.selected_id == widget.id;
        const bool disabled = (widget.flags & KU_UI_WIDGET_DISABLED) != 0U;

        if (widget.type == KU_UI_WIDGET_PANEL) {
            font::draw(font::Face::Display, bounds.x + 38, y + 5,
                       "BLADE LAUNCHER", kText, kObsidian, 2U, true);
            graphics::fill_rect(bounds.x + 38, y + 28,
                                bounds.width > 230 ? 168 : bounds.width - 74,
                                2, kCrimson);
            draw_icon(widget, bounds.x + bounds.width - 46, y + 4, 26);
        } else if (widget.type == KU_UI_WIDGET_LABEL) {
            const graphics::Color color = widget.id == 31U ? kAsh : kMuted;
            font::draw(font::Face::Ui, bounds.x + 39, y + 6,
                       widget.text, color, kObsidian, 1U, true);
        } else if (widget.type == KU_UI_WIDGET_LIST_ITEM ||
                   widget.type == KU_UI_WIDGET_BUTTON) {
            const int32_t card_x = bounds.x + 34;
            int32_t card_width = bounds.width - 48;
            const int32_t taper = static_cast<int32_t>(blade_index % 4U) * 5;
            card_width -= taper;
            const graphics::Color fill = selected ? kSteelActive : kSteel;
            const graphics::Color edge = selected ? kHotEdge : kSteelEdge;
            outline_blade_card(card_x, y + 1, card_width, height - 5, edge, fill);
            if (selected) {
                graphics::fill_rect(card_x + 3, y + 6, 4, height - 16, kHotEdge);
                graphics::fill_rect(card_x + 7, y + 8, 1, height - 20, kCrimson);
                graphics::fill_rect(card_x + 16, y + 1,
                                    card_width > 92 ? 74 : card_width - 24,
                                    2, kHotEdge);
            } else {
                graphics::fill_rect(card_x + 4, y + 8, 1, height - 20, kCrimson);
            }
            draw_icon(widget, card_x + 13, y + 5, 24);
            font::draw(font::Face::Ui, card_x + 47, y + 9,
                       widget.text,
                       disabled ? kMuted : (selected ? kText : kAsh),
                       fill, 1U, true);

            // 01..09 rail markers mirror the reference launcher's indexed
            // spine and make keyboard selection visually obvious.
            if (blade_index < 9U) {
                char marker[3] = {'0', (char)('1' + blade_index), '\0'};
                font::draw(font::Face::Mono, rail_x - 1, y + 10,
                           marker, selected ? kHotEdge : kMuted,
                           kSteelDeep, 1U, true);
            }
            ++blade_index;
        } else if (widget.type == KU_UI_WIDGET_SEPARATOR) {
            graphics::fill_rect(bounds.x + 39, y + 5, bounds.width - 68, 1,
                                kSteelEdge);
            graphics::fill_rect(bounds.x + 39, y + 5, 68, 1, kCrimson);
        }
        y += height;
    }
}

void draw_generic_surface(
    const Rect& bounds,
    const ku_ui_surface& surface,
    bool focused,
    bool kurosh,
    bool access,
    bool forge,
    bool pulse,
    bool anvil) {
    draw_surface_frame(bounds, focused, access);
    draw_micro_etch(bounds);

    // Forge/Pulse/Anvil get slightly tighter horizontal framing to make them
    // read like control boards instead of generic document windows.
    const int32_t side = access ? 18 : ((forge || pulse || anvil) ? 16 : 12);
    const int32_t left = bounds.x + side;
    const int32_t right = bounds.x + bounds.width - side;
    int32_t y = bounds.y + 10;
    uint32_t visible_index = 0U;
    uint32_t rendered = 0U;
    const uint32_t row_limit = surface.visible_rows == 0U
        ? KU_UI_MAX_WIDGETS : surface.visible_rows;

    for (uint32_t index = 0U; index < surface.widget_count; ++index) {
        const ku_ui_widget& widget = surface.widgets[index];
        if ((widget.flags & KU_UI_WIDGET_HIDDEN) != 0U) continue;
        if (visible_index++ < surface.scroll_offset) continue;
        if (rendered++ >= row_limit) break;

        const int32_t height = row_height(widget.type);
        if (y >= bounds.y + bounds.height) break;
        const uint32_t depth = widget_depth(surface, widget);
        const int32_t indent = static_cast<int32_t>(depth) * (access ? 10 : 14);
        const Rect row{left + indent, y, right - left - indent, height - 4};
        if (row.width <= 0) break;

        const bool selected =
            (widget.flags & KU_UI_WIDGET_SELECTED) != 0U ||
            surface.selected_id == widget.id;
        const bool disabled = (widget.flags & KU_UI_WIDGET_DISABLED) != 0U;
        const graphics::Color text_color = disabled ? kMuted : kText;
        const bool has_icon = widget.icon_id != 0U &&
            icons::valid(static_cast<ku_icon_id_t>(widget.icon_id));
        const int32_t icon_size = widget.type == KU_UI_WIDGET_PANEL ? 24 : 20;
        const int32_t text_x = row.x + (has_icon ? icon_size + 14 : 11);
        const font::Face face = text_face(kurosh, widget);

        switch (widget.type) {
            case KU_UI_WIDGET_PANEL: {
                const graphics::Color fill = selected ? kSteelRaised : kSteel;
                const graphics::Color edge = selected
                    ? kHotEdge
                    : (depth == 0U && focused ? kCrimson : kSteelEdge);
                fill_chamfered(row.x + 3, row.y + 3, row.width, row.height, 6, kShadow);
                outline_chamfered(row.x, row.y, row.width, row.height, 6, edge, fill);
                graphics::fill_rect(row.x + 8, row.y + 1,
                                    row.width > 118 ? 104 : row.width - 16,
                                    1, selected ? kHotEdge : kCrimson);
                graphics::fill_rect(row.x + 5, row.y + 7,
                                    selected ? 3 : 2,
                                    row.height > 14 ? row.height - 14 : 1,
                                    selected ? kHotEdge : kCrimson);
                draw_icon(widget, row.x + 10, row.y + 6, icon_size);
                font::draw(face, text_x, row.y + 10, widget.text,
                           text_color, fill, depth == 0U ? 2U : 1U, true);
                break;
            }
            case KU_UI_WIDGET_LABEL: {
                draw_icon(widget, row.x + 4, row.y + 1, icon_size);
                const graphics::Color label_color = disabled ? kMuted : kAsh;
                font::draw(face, text_x, row.y + 6, widget.text,
                           label_color, kObsidian, 1U, true);
                break;
            }
            case KU_UI_WIDGET_BUTTON:
            case KU_UI_WIDGET_INPUT:
            case KU_UI_WIDGET_LIST_ITEM: {
                const graphics::Color fill = selected ? kSteelActive : kSteel;
                const graphics::Color edge = selected ? kHotEdge : kSteelEdge;
                fill_chamfered(row.x + 2, row.y + 3, row.width, row.height, 5, kShadow);
                outline_chamfered(row.x, row.y, row.width, row.height, 5, edge, fill);
                graphics::fill_rect(row.x + 5, row.y + 7,
                                    selected ? 3 : 1,
                                    row.height > 14 ? row.height - 14 : 1,
                                    selected ? kHotEdge : kCrimson);
                if (widget.type == KU_UI_WIDGET_INPUT) {
                    graphics::fill_rect(row.x + 11, row.y + row.height - 4,
                                        row.width - 22, 1,
                                        selected ? kHotEdge : kSteelEdge);
                }
                draw_icon(widget, row.x + 9, row.y + 6, icon_size);
                font::draw(face, text_x, row.y + 11, widget.text,
                           text_color, fill, 1U, true);
                if (selected) {
                    graphics::fill_rect(row.x + row.width - 10,
                                        row.y + row.height / 2 - 4,
                                        2, 9, kHotEdge);
                }
                break;
            }
            case KU_UI_WIDGET_PROGRESS: {
                outline_chamfered(row.x, row.y, row.width, row.height, 5,
                                  kSteelEdge, kSteel);
                draw_icon(widget, row.x + 9, row.y + 6, icon_size);
                font::draw(face, text_x, row.y + 8, widget.text,
                           text_color, kSteel, 1U, true);
                const uint32_t maximum = widget.maximum == 0U ? 1U : widget.maximum;
                const uint32_t value = widget.value > maximum ? maximum : widget.value;
                const int32_t bar_x = row.x + 10;
                const int32_t bar_y = row.y + row.height - 14;
                const int32_t bar_width = row.width - 20;
                graphics::fill_rect(bar_x, bar_y, bar_width, 7,
                                    graphics::rgb(38, 45, 52));
                graphics::fill_rect(
                    bar_x, bar_y,
                    static_cast<int32_t>((static_cast<uint64_t>(bar_width) * value) / maximum),
                    7, kCrimson);
                graphics::fill_rect(bar_x, bar_y,
                                    bar_width > 44 ? 44 : bar_width,
                                    1, kHotEdge);
                break;
            }
            case KU_UI_WIDGET_SEPARATOR:
                graphics::fill_rect(row.x, row.y + 4, row.width, 1, kSteelEdge);
                graphics::fill_rect(row.x, row.y + 4,
                                    row.width > 88 ? 88 : row.width,
                                    1, kCrimson);
                break;
            default:
                break;
        }
        y += height;
    }
}

} // namespace

void forged_surface(
    const Rect& bounds,
    const ku_ui_surface& surface,
    bool focused) {
    if (bounds.width <= 0 || bounds.height <= 0) return;

    if (is_blade_surface(surface)) {
        draw_blade_surface(bounds, surface, focused);
        return;
    }

    draw_generic_surface(
        bounds,
        surface,
        focused,
        is_kurosh_surface(surface),
        is_access_surface(surface),
        is_forge_surface(surface),
        is_pulse_surface(surface),
        is_anvil_surface(surface));
}

} // namespace ui

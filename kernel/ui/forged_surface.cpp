#include "forged_surface.hpp"

#include "font.hpp"
#include "icon_registry.hpp"

namespace ui {
namespace {

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

enum class SurfaceKind : uint8_t {
    Generic = 0,
    Blade,
    Kurosh,
    Access,
    Forge,
    Pulse,
    Anvil,
    Vault,
    Web,
};

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
    graphics::Color fill,
    bool shadow) {
    if (shadow) {
        fill_chamfered(x + 3, y + 4, width, height, cut, kShadow);
    }
    fill_chamfered(x, y, width, height, cut, edge);
    fill_chamfered(x + 1, y + 1, width - 2, height - 2,
                   cut > 0 ? cut - 1 : 0, fill);
}

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

SurfaceKind surface_kind(const ku_ui_surface& surface) {
    for (uint32_t index = 0U; index < surface.widget_count; ++index) {
        const ku_ui_widget& widget = surface.widgets[index];
        if ((widget.flags & KU_UI_WIDGET_HIDDEN) != 0U ||
            widget.type != KU_UI_WIDGET_PANEL) continue;
        if (starts_with(widget.text, "BLADE LAUNCHER")) return SurfaceKind::Blade;
        if (starts_with(widget.text, "KUROSH")) return SurfaceKind::Kurosh;
        if (starts_with(widget.text, "SECURE ACCESS") ||
            starts_with(widget.text, "BEZPIECZNY DOSTEP")) return SurfaceKind::Access;
        if (starts_with(widget.text, "FORGE CONTROL")) return SurfaceKind::Forge;
        if (starts_with(widget.text, "PULSE")) return SurfaceKind::Pulse;
        if (starts_with(widget.text, "ANVIL")) return SurfaceKind::Anvil;
        if (starts_with(widget.text, "VAULT")) return SurfaceKind::Vault;
        if (starts_with(widget.text, "KUROGANE WEB")) return SurfaceKind::Web;
        return SurfaceKind::Generic;
    }
    return SurfaceKind::Generic;
}

bool has_spatial_widgets(const ku_ui_surface& surface) {
    for (uint32_t index = 0U; index < surface.widget_count; ++index) {
        if (ku_ui_widget_has_absolute_layout(&surface.widgets[index])) return true;
    }
    return false;
}

font::Face text_face(SurfaceKind kind, const ku_ui_widget& widget) {
    if (widget.type == KU_UI_WIDGET_PANEL) return font::Face::Display;
    if (kind == SurfaceKind::Kurosh && widget.type != KU_UI_WIDGET_BUTTON) {
        return font::Face::Mono;
    }
    return font::Face::Ui;
}

void draw_micro_etch(const Rect& bounds) {
    if (bounds.width < 180 || bounds.height < 100) return;
    for (int32_t y = bounds.y + 50; y < bounds.y + bounds.height; y += 72) {
        graphics::fill_rect(bounds.x + 18, y, bounds.width - 36, 1,
                            graphics::rgb(16, 22, 26));
    }
    for (int32_t y = bounds.y + 74; y < bounds.y + bounds.height; y += 144) {
        graphics::fill_rect(bounds.x + bounds.width - 62, y, 36, 1,
                            graphics::rgb(55, 29, 33));
    }
}

void draw_surface_frame(
    const Rect& bounds,
    bool focused,
    SurfaceKind kind) {
    if (kind == SurfaceKind::Access) {
        outline_chamfered(bounds.x, bounds.y, bounds.width, bounds.height,
                          9, focused ? kHotEdge : kSteelEdge,
                          kObsidian, true);
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

void draw_standard_widget(
    const ku_ui_widget& widget,
    const Rect& row,
    SurfaceKind kind,
    bool focused,
    bool selected) {
    if (row.width <= 0 || row.height <= 0) return;
    const bool disabled = (widget.flags & KU_UI_WIDGET_DISABLED) != 0U;
    const graphics::Color foreground = disabled
        ? kMuted : (selected ? kText : kAsh);
    const bool has_icon = widget.icon_id != 0U &&
        icons::valid(static_cast<ku_icon_id_t>(widget.icon_id));
    const int32_t icon_size = widget.type == KU_UI_WIDGET_PANEL ? 24 : 20;
    const int32_t text_x = row.x + (has_icon ? icon_size + 18 : 11);
    const font::Face face = text_face(kind, widget);

    switch (widget.type) {
        case KU_UI_WIDGET_PANEL: {
            const graphics::Color fill = selected ? kSteelRaised : kSteel;
            const graphics::Color edge = selected
                ? kHotEdge : (focused ? kCrimson : kSteelEdge);
            outline_chamfered(row.x, row.y, row.width, row.height, 6,
                              edge, fill, true);
            graphics::fill_rect(row.x + 8, row.y + 1,
                                row.width > 112 ? 96 : row.width - 16,
                                1, selected ? kHotEdge : kCrimson);
            graphics::fill_rect(row.x + 5, row.y + 7,
                                selected ? 3 : 2,
                                row.height > 14 ? row.height - 14 : 1,
                                selected ? kHotEdge : kCrimson);
            draw_icon(widget, row.x + 10,
                      row.y + (row.height - icon_size) / 2, icon_size);
            font::draw(face, text_x,
                       row.y + (row.height >= 42 ? 13 : 10),
                       widget.text, disabled ? kMuted : kText,
                       fill, row.height >= 42 ? 2U : 1U, true);
            break;
        }
        case KU_UI_WIDGET_LABEL:
            draw_icon(widget, row.x + 4,
                      row.y + (row.height - icon_size) / 2, icon_size);
            font::draw(face, text_x, row.y + 7,
                       widget.text, foreground, kObsidian, 1U, true);
            break;
        case KU_UI_WIDGET_BUTTON:
        case KU_UI_WIDGET_INPUT:
        case KU_UI_WIDGET_LIST_ITEM: {
            const graphics::Color fill = selected ? kSteelActive : kSteel;
            const graphics::Color edge = selected ? kHotEdge : kSteelEdge;
            outline_chamfered(row.x, row.y, row.width, row.height, 5,
                              edge, fill, true);
            graphics::fill_rect(row.x + 5, row.y + 7,
                                selected ? 3 : 1,
                                row.height > 14 ? row.height - 14 : 1,
                                selected ? kHotEdge : kCrimson);
            if (widget.type == KU_UI_WIDGET_INPUT) {
                graphics::fill_rect(row.x + 11, row.y + row.height - 4,
                                    row.width - 22, 1,
                                    selected ? kHotEdge : kSteelEdge);
            }
            draw_icon(widget, row.x + 9,
                      row.y + (row.height - icon_size) / 2, icon_size);
            font::draw(face, text_x,
                       row.y + (row.height - 8) / 2,
                       widget.text, foreground, fill, 1U, true);
            break;
        }
        case KU_UI_WIDGET_PROGRESS: {
            outline_chamfered(row.x, row.y, row.width, row.height, 5,
                              kSteelEdge, kSteel, false);
            font::draw(face, row.x + 10, row.y + 8,
                       widget.text, foreground, kSteel, 1U, true);
            const uint32_t maximum = widget.maximum == 0U ? 1U : widget.maximum;
            const uint32_t value = widget.value > maximum ? maximum : widget.value;
            const int32_t bar_x = row.x + 10;
            const int32_t bar_y = row.y + row.height - 14;
            const int32_t bar_width = row.width - 20;
            graphics::fill_rect(bar_x, bar_y, bar_width, 7,
                                graphics::rgb(38, 45, 52));
            graphics::fill_rect(
                bar_x, bar_y,
                static_cast<int32_t>(
                    (static_cast<uint64_t>(bar_width) * value) / maximum),
                7, kCrimson);
            break;
        }
        case KU_UI_WIDGET_SEPARATOR:
            graphics::fill_rect(row.x, row.y + row.height / 2,
                                row.width, 1, kSteelEdge);
            graphics::fill_rect(row.x, row.y + row.height / 2,
                                row.width > 88 ? 88 : row.width,
                                1, kCrimson);
            break;
        default:
            break;
    }
}

void draw_blade_surface(
    const Rect& bounds,
    const ku_ui_surface& surface,
    bool focused) {
    draw_surface_frame(bounds, focused, SurfaceKind::Blade);
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
            font::draw(font::Face::Ui, bounds.x + 39, y + 6,
                       widget.text, widget.id == 31U ? kAsh : kMuted,
                       kObsidian, 1U, true);
        } else if (widget.type == KU_UI_WIDGET_LIST_ITEM ||
                   widget.type == KU_UI_WIDGET_BUTTON) {
            const int32_t card_x = bounds.x + 34;
            int32_t card_width = bounds.width - 48 -
                static_cast<int32_t>(blade_index % 4U) * 5;
            const graphics::Color fill = selected ? kSteelActive : kSteel;
            const graphics::Color edge = selected ? kHotEdge : kSteelEdge;
            outline_blade_card(card_x, y + 1, card_width, height - 5,
                               edge, fill);
            graphics::fill_rect(card_x + 4, y + 8,
                                selected ? 3 : 1,
                                height - 20,
                                selected ? kHotEdge : kCrimson);
            draw_icon(widget, card_x + 13, y + 5, 24);
            font::draw(font::Face::Ui, card_x + 47, y + 9,
                       widget.text,
                       disabled ? kMuted : (selected ? kText : kAsh),
                       fill, 1U, true);
            if (blade_index < 9U) {
                char marker[3] = {'0', (char)('1' + blade_index), '\0'};
                font::draw(font::Face::Mono, rail_x - 1, y + 10,
                           marker, selected ? kHotEdge : kMuted,
                           kSteelDeep, 1U, true);
            }
            ++blade_index;
        } else if (widget.type == KU_UI_WIDGET_SEPARATOR) {
            graphics::fill_rect(bounds.x + 39, y + 5,
                                bounds.width - 68, 1, kSteelEdge);
            graphics::fill_rect(bounds.x + 39, y + 5, 68, 1, kCrimson);
        }
        y += height;
    }
}

void draw_flow_surface(
    const Rect& bounds,
    const ku_ui_surface& surface,
    bool focused,
    SurfaceKind kind) {
    draw_surface_frame(bounds, focused, kind);
    draw_micro_etch(bounds);

    const bool control_board = kind == SurfaceKind::Forge ||
        kind == SurfaceKind::Pulse || kind == SurfaceKind::Anvil;
    const int32_t side = kind == SurfaceKind::Access
        ? 18 : (control_board ? 16 : 12);
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
        if (y + height > bounds.y + bounds.height) break;
        const int32_t indent = static_cast<int32_t>(
            widget_depth(surface, widget)) * 14;
        const Rect row{left + indent, y,
                       right - left - indent, height - 4};
        draw_standard_widget(
            widget, row, kind, focused,
            (widget.flags & KU_UI_WIDGET_SELECTED) != 0U ||
                surface.selected_id == widget.id);
        y += height;
    }
}

void draw_spatial_surface(
    const Rect& bounds,
    const ku_ui_surface& surface,
    bool focused,
    SurfaceKind kind) {
    draw_surface_frame(bounds, focused, kind);
    draw_micro_etch(bounds);

    int32_t fallback_y = bounds.y + 10;
    uint32_t visible_index = 0U;
    for (uint32_t index = 0U; index < surface.widget_count; ++index) {
        const ku_ui_widget& widget = surface.widgets[index];
        if ((widget.flags & KU_UI_WIDGET_HIDDEN) != 0U) continue;
        const bool selected =
            (widget.flags & KU_UI_WIDGET_SELECTED) != 0U ||
            surface.selected_id == widget.id;

        if (ku_ui_widget_has_absolute_layout(&widget)) {
            const Rect row{
                bounds.x + KU_UI_LAYOUT_X(widget.value),
                bounds.y + KU_UI_LAYOUT_Y(widget.value),
                KU_UI_LAYOUT_WIDTH(widget.maximum),
                KU_UI_LAYOUT_HEIGHT(widget.maximum)};
            draw_standard_widget(widget, row, kind, focused, selected);
            continue;
        }

        // Progress retains value/maximum semantics and older non-spatial rows
        // remain valid while an application migrates incrementally.
        if (visible_index++ < surface.scroll_offset) continue;
        if (surface.visible_rows != 0U &&
            visible_index > surface.scroll_offset + surface.visible_rows) break;
        const int32_t height = row_height(widget.type);
        const Rect row{bounds.x + 12, fallback_y,
                       bounds.width - 24, height - 4};
        draw_standard_widget(widget, row, kind, focused, selected);
        fallback_y += height;
    }
}

} // namespace

void forged_surface(
    const Rect& bounds,
    const ku_ui_surface& surface,
    bool focused) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const SurfaceKind kind = surface_kind(surface);
    if (has_spatial_widgets(surface)) {
        draw_spatial_surface(bounds, surface, focused, kind);
    } else if (kind == SurfaceKind::Blade) {
        draw_blade_surface(bounds, surface, focused);
    } else {
        draw_flow_surface(bounds, surface, focused, kind);
    }
}

} // namespace ui

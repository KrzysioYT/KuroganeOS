#include "forged_surface.hpp"

#include "font.hpp"
#include "icon_registry.hpp"

namespace ui {
namespace {

constexpr graphics::Color kObsidian = graphics::rgb(9, 14, 14);
constexpr graphics::Color kSteel = graphics::rgb(23, 28, 34);
constexpr graphics::Color kSteelRaised = graphics::rgb(32, 38, 45);
constexpr graphics::Color kSteelActive = graphics::rgb(42, 31, 36);
constexpr graphics::Color kSteelEdge = graphics::rgb(52, 59, 67);
constexpr graphics::Color kAsh = graphics::rgb(168, 175, 184);
constexpr graphics::Color kText = graphics::rgb(238, 241, 244);
constexpr graphics::Color kMuted = graphics::rgb(137, 145, 154);
constexpr graphics::Color kCrimson = graphics::rgb(230, 41, 50);
constexpr graphics::Color kHotEdge = graphics::rgb(255, 74, 69);
constexpr graphics::Color kShadow = graphics::rgb(3, 6, 7);

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

void draw_icon(const ku_ui_widget& widget, int32_t x, int32_t y, int32_t size) {
    const auto icon = static_cast<ku_icon_id_t>(widget.icon_id);
    if (icon == KU_ICON_NONE || !icons::valid(icon)) return;
    icons::draw(icon, x, y, size, size);
}

bool is_kurosh_surface(const ku_ui_surface& surface) {
    for (uint32_t index = 0U; index < surface.widget_count; ++index) {
        if ((surface.widgets[index].flags & KU_UI_WIDGET_HIDDEN) != 0U) continue;
        if (starts_with(surface.widgets[index].text, "KUROSH")) return true;
        if (surface.widgets[index].type == KU_UI_WIDGET_PANEL) return false;
    }
    return false;
}

bool is_access_surface(const ku_ui_surface& surface) {
    for (uint32_t index = 0U; index < surface.widget_count; ++index) {
        const ku_ui_widget& widget = surface.widgets[index];
        if ((widget.flags & KU_UI_WIDGET_HIDDEN) != 0U) continue;
        if (widget.type != KU_UI_WIDGET_PANEL) continue;
        if (starts_with(widget.text, "SECURE ACCESS") ||
            starts_with(widget.text, "BEZPIECZNY DOSTEP")) {
            return true;
        }
        if (widget.parent_id == 0U) return false;
    }
    return false;
}

font::Face text_face(bool kurosh, const ku_ui_widget& widget) {
    if (widget.type == KU_UI_WIDGET_PANEL) return font::Face::Display;
    if (kurosh && widget.type != KU_UI_WIDGET_BUTTON) return font::Face::Mono;
    return font::Face::Ui;
}

void draw_micro_etch(const Rect& bounds) {
    if (bounds.width < 180 || bounds.height < 100) return;
    for (int32_t y = bounds.y + 52; y < bounds.y + bounds.height; y += 96) {
        graphics::fill_rect(
            bounds.x + bounds.width - 58,
            y,
            34,
            1,
            graphics::rgb(21, 27, 31));
        graphics::fill_rect(
            bounds.x + bounds.width - 31,
            y - 4,
            1,
            5,
            graphics::rgb(77, 28, 31));
    }
}

} // namespace

void forged_surface(
    const Rect& bounds,
    const ku_ui_surface& surface,
    bool focused) {
    if (bounds.width <= 0 || bounds.height <= 0) return;

    const graphics::Color requested_background =
        surface.background_rgb & UINT32_C(0xFFFFFF);
    const graphics::Color background = requested_background == 0U
        ? kObsidian : requested_background;
    const graphics::Color requested_foreground =
        surface.foreground_rgb & UINT32_C(0xFFFFFF);
    const graphics::Color foreground = requested_foreground == 0U
        ? kText : requested_foreground;
    const graphics::Color requested_accent =
        surface.accent_rgb & UINT32_C(0xFFFFFF);
    const graphics::Color accent = requested_accent == 0U
        ? kCrimson : requested_accent;
    const bool kurosh = is_kurosh_surface(surface);
    const bool access = is_access_surface(surface);

    if (access) {
        fill_chamfered(
            bounds.x + 4, bounds.y + 5,
            bounds.width - 4, bounds.height - 5,
            8, kShadow);
        outline_chamfered(
            bounds.x, bounds.y,
            bounds.width, bounds.height,
            8, kSteelEdge, background);
        graphics::fill_rect(
            bounds.x + 18, bounds.y + 1,
            bounds.width > 220 ? 176 : bounds.width - 36,
            2, kCrimson);
        if (bounds.width > 180) {
            graphics::fill_rect(
                bounds.x + bounds.width - 78, bounds.y + 10,
                42, 1, graphics::rgb(63, 33, 37));
        }
    } else {
        graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, background);
        graphics::fill_rect(bounds.x, bounds.y, 2, bounds.height,
                            focused ? kHotEdge : kSteelEdge);
        graphics::fill_rect(bounds.x + 2, bounds.y, 74, 1,
                            focused ? kCrimson : kSteelEdge);
    }
    draw_micro_etch(bounds);

    const int32_t left = bounds.x + (access ? 18 : 12);
    const int32_t right = bounds.x + bounds.width - (access ? 18 : 12);
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
        const graphics::Color text_color = disabled ? kMuted : foreground;
        const bool has_icon = widget.icon_id != 0U &&
            icons::valid(static_cast<ku_icon_id_t>(widget.icon_id));
        const int32_t icon_size = widget.type == KU_UI_WIDGET_PANEL ? 24 : 20;
        const int32_t text_x = row.x + (has_icon ? icon_size + 14 : 11);
        const font::Face face = text_face(kurosh, widget);

        switch (widget.type) {
            case KU_UI_WIDGET_PANEL: {
                if (access && depth == 0U) {
                    fill_chamfered(
                        row.x + 3, row.y + 3,
                        row.width, row.height,
                        6, kShadow);
                    outline_chamfered(
                        row.x, row.y,
                        row.width, row.height,
                        6, kSteelEdge, kSteel);
                    graphics::fill_rect(
                        row.x + 7, row.y + 7,
                        3, row.height > 14 ? row.height - 14 : 1,
                        kHotEdge);
                    graphics::fill_rect(
                        row.x + 14, row.y + row.height - 2,
                        row.width > 230 ? 198 : row.width - 28,
                        2, kCrimson);
                    draw_icon(widget, row.x + 14, row.y + 6, icon_size);
                    font::draw(
                        face, text_x + 4, row.y + 7,
                        widget.text, kText, kSteel, 2U, true);
                    break;
                }

                const graphics::Color fill = selected ? kSteelRaised : kSteel;
                const graphics::Color edge = selected
                    ? kHotEdge
                    : (!access && depth == 0U && focused ? kCrimson : kSteelEdge);
                fill_chamfered(row.x + 3, row.y + 3, row.width, row.height, 6, kShadow);
                outline_chamfered(
                    row.x, row.y, row.width, row.height, 6,
                    edge, fill);
                graphics::fill_rect(
                    row.x + 8, row.y + 1,
                    row.width > 92 ? (access ? 54 : 82) : row.width - 16,
                    1, selected ? kHotEdge : (depth == 0U ? kCrimson : kSteelEdge));
                graphics::fill_rect(row.x + 5, row.y + 7, selected ? 3 : 2,
                                    row.height > 14 ? row.height - 14 : 1,
                                    selected ? kHotEdge : accent);
                draw_icon(widget, row.x + 10, row.y + 6, icon_size);
                font::draw(face, text_x, row.y + 10, widget.text,
                           text_color, fill, 1U, true);
                break;
            }
            case KU_UI_WIDGET_LABEL: {
                draw_icon(widget, row.x + 4, row.y + 1, icon_size);
                graphics::Color label_color = disabled ? kMuted : kAsh;
                if (access && (widget.id == 3U || widget.id == 24U)) {
                    label_color = kMuted;
                }
                font::draw(face, text_x, row.y + 6, widget.text,
                           label_color, background, 1U, true);
                break;
            }
            case KU_UI_WIDGET_BUTTON:
            case KU_UI_WIDGET_INPUT:
            case KU_UI_WIDGET_LIST_ITEM: {
                const graphics::Color fill = selected
                    ? (access ? kSteelActive : kSteelRaised)
                    : kSteel;
                const graphics::Color edge = selected ? kHotEdge : kSteelEdge;
                fill_chamfered(row.x + 2, row.y + 3, row.width, row.height, 5, kShadow);
                outline_chamfered(row.x, row.y, row.width, row.height, 5, edge, fill);
                graphics::fill_rect(row.x + 5, row.y + 7,
                                    selected ? (access ? 4 : 3) : 1,
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
                if (selected && access) {
                    const int32_t marker_width = row.width > 118 ? 86 : row.width - 24;
                    if (marker_width > 0) {
                        graphics::fill_rect(
                            row.x + row.width - marker_width - 12,
                            row.y + row.height - 3,
                            marker_width, 2, kCrimson);
                    }
                } else if (selected && widget.type == KU_UI_WIDGET_LIST_ITEM) {
                    graphics::fill_rect(row.x + row.width - 9,
                                        row.y + row.height / 2 - 3,
                                        2, 7, kHotEdge);
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
                uint32_t value = widget.value > maximum ? maximum : widget.value;
                const int32_t bar_x = row.x + 10;
                const int32_t bar_y = row.y + row.height - 14;
                const int32_t bar_width = row.width - 20;
                graphics::fill_rect(bar_x, bar_y, bar_width, 7, graphics::rgb(42, 49, 56));
                graphics::fill_rect(
                    bar_x, bar_y,
                    static_cast<int32_t>((static_cast<uint64_t>(bar_width) * value) / maximum),
                    7, accent);
                graphics::fill_rect(bar_x, bar_y, bar_width > 42 ? 42 : bar_width,
                                    1, kHotEdge);
                break;
            }
            case KU_UI_WIDGET_SEPARATOR:
                graphics::fill_rect(row.x, row.y + 4, row.width, 1, kSteelEdge);
                graphics::fill_rect(row.x, row.y + 4,
                                    row.width > 82 ? (access ? 48 : 82) : row.width,
                                    1, kCrimson);
                break;
            default:
                break;
        }
        y += height;
    }
}

} // namespace ui

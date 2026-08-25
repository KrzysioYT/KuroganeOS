#include "spatial_surface.hpp"

#include "font.hpp"
#include "forged_surface.hpp"
#include "icon_registry.hpp"

namespace ui {
namespace {

constexpr graphics::Color kObsidian = graphics::rgb(9, 14, 14);
constexpr graphics::Color kSteel = graphics::rgb(23, 28, 34);
constexpr graphics::Color kSteelRaised = graphics::rgb(31, 37, 44);
constexpr graphics::Color kSteelEdge = graphics::rgb(52, 59, 67);
constexpr graphics::Color kAsh = graphics::rgb(168, 175, 184);
constexpr graphics::Color kText = graphics::rgb(238, 241, 244);
constexpr graphics::Color kMuted = graphics::rgb(122, 132, 143);
constexpr graphics::Color kCrimson = graphics::rgb(230, 41, 50);
constexpr graphics::Color kHotEdge = graphics::rgb(255, 74, 69);
constexpr graphics::Color kShadow = graphics::rgb(2, 4, 5);

int32_t flow_height(uint32_t type) {
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

bool has_spatial_widgets(const ku_ui_surface& surface) {
    for (uint32_t index = 0U; index < surface.widget_count; ++index) {
        if (ku_ui_widget_has_absolute_layout(&surface.widgets[index])) return true;
    }
    return false;
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

void panel_shape(
    const Rect& rectangle,
    graphics::Color edge,
    graphics::Color fill,
    bool shadow) {
    if (shadow) {
        fill_chamfered(rectangle.x + 3, rectangle.y + 4,
                       rectangle.width, rectangle.height, 6, kShadow);
    }
    fill_chamfered(rectangle.x, rectangle.y,
                   rectangle.width, rectangle.height, 6, edge);
    fill_chamfered(rectangle.x + 1, rectangle.y + 1,
                   rectangle.width - 2, rectangle.height - 2, 5, fill);
}

void draw_icon(const ku_ui_widget& widget, const Rect& rectangle, int32_t size) {
    const ku_icon_id_t icon = static_cast<ku_icon_id_t>(widget.icon_id);
    if (icon == KU_ICON_NONE || !icons::valid(icon)) return;
    icons::draw(icon,
                rectangle.x + 10,
                rectangle.y + (rectangle.height - size) / 2,
                size, size);
}

void draw_spatial_widget(
    const ku_ui_widget& widget,
    const Rect& rectangle,
    bool focused,
    bool selected) {
    if (rectangle.width <= 0 || rectangle.height <= 0) return;
    const bool disabled = (widget.flags & KU_UI_WIDGET_DISABLED) != 0U;
    const graphics::Color foreground = disabled
        ? kMuted : (selected ? kText : kAsh);
    const int32_t icon_size = widget.type == KU_UI_WIDGET_PANEL ? 24 : 19;
    const bool has_icon = widget.icon_id != 0U &&
        icons::valid(static_cast<ku_icon_id_t>(widget.icon_id));
    const int32_t text_x = rectangle.x + (has_icon ? icon_size + 18 : 12);

    switch (widget.type) {
        case KU_UI_WIDGET_PANEL: {
            const graphics::Color edge = selected
                ? kHotEdge : (focused ? kCrimson : kSteelEdge);
            const graphics::Color fill = selected ? kSteelRaised : kSteel;
            panel_shape(rectangle, edge, fill, true);
            graphics::fill_rect(rectangle.x + 12, rectangle.y + 1,
                                rectangle.width > 112 ? 92 : rectangle.width - 24,
                                1, selected ? kHotEdge : kCrimson);
            graphics::fill_rect(rectangle.x + 6, rectangle.y + 8,
                                selected ? 3 : 2,
                                rectangle.height > 16 ? rectangle.height - 16 : 1,
                                selected ? kHotEdge : kCrimson);
            draw_icon(widget, rectangle, icon_size);
            font::draw(font::Face::Display, text_x, rectangle.y + 11,
                       widget.text, disabled ? kMuted : kText,
                       fill, rectangle.height >= 42 ? 2U : 1U, true);
            break;
        }
        case KU_UI_WIDGET_LABEL: {
            draw_icon(widget, rectangle, icon_size);
            font::draw(font::Face::Ui, text_x, rectangle.y + 7,
                       widget.text, foreground, kObsidian, 1U, true);
            break;
        }
        case KU_UI_WIDGET_BUTTON:
        case KU_UI_WIDGET_INPUT:
        case KU_UI_WIDGET_LIST_ITEM: {
            const graphics::Color fill = selected
                ? graphics::rgb(45, 28, 33) : kSteel;
            panel_shape(rectangle,
                        selected ? kHotEdge : kSteelEdge,
                        fill, true);
            graphics::fill_rect(rectangle.x + 5, rectangle.y + 7,
                                selected ? 3 : 1,
                                rectangle.height > 14 ? rectangle.height - 14 : 1,
                                selected ? kHotEdge : kCrimson);
            if (widget.type == KU_UI_WIDGET_INPUT) {
                graphics::fill_rect(rectangle.x + 12,
                                    rectangle.y + rectangle.height - 5,
                                    rectangle.width - 24, 1,
                                    selected ? kHotEdge : kSteelEdge);
            }
            draw_icon(widget, rectangle, icon_size);
            font::draw(font::Face::Ui, text_x,
                       rectangle.y + (rectangle.height - 8) / 2,
                       widget.text, foreground, fill, 1U, true);
            break;
        }
        case KU_UI_WIDGET_SEPARATOR:
            graphics::fill_rect(rectangle.x,
                                rectangle.y + rectangle.height / 2,
                                rectangle.width, 1, kSteelEdge);
            graphics::fill_rect(rectangle.x,
                                rectangle.y + rectangle.height / 2,
                                rectangle.width > 74 ? 74 : rectangle.width,
                                1, kCrimson);
            break;
        default:
            break;
    }
}

void draw_progress_flow(
    const ku_ui_widget& widget,
    const Rect& rectangle) {
    if (rectangle.width <= 0 || rectangle.height <= 0) return;
    panel_shape(rectangle, kSteelEdge, kSteel, false);
    font::draw(font::Face::Ui,
               rectangle.x + 10, rectangle.y + 8,
               widget.text, kAsh, kSteel, 1U, true);
    const uint32_t maximum = widget.maximum == 0U ? 1U : widget.maximum;
    const uint32_t value = widget.value > maximum ? maximum : widget.value;
    const int32_t bar_x = rectangle.x + 10;
    const int32_t bar_y = rectangle.y + rectangle.height - 14;
    const int32_t bar_width = rectangle.width - 20;
    graphics::fill_rect(bar_x, bar_y, bar_width, 6,
                        graphics::rgb(38, 45, 52));
    graphics::fill_rect(bar_x, bar_y,
                        static_cast<int32_t>(
                            (static_cast<uint64_t>(bar_width) * value) / maximum),
                        6, kCrimson);
}

} // namespace

void spatial_surface(
    const Rect& bounds,
    const ku_ui_surface& surface,
    bool focused) {
    if (!has_spatial_widgets(surface)) {
        forged_surface(bounds, surface, focused);
        return;
    }

    graphics::fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, kObsidian);
    graphics::fill_rect(bounds.x, bounds.y, 2, bounds.height,
                        focused ? kHotEdge : kSteelEdge);

    int32_t flow_y = bounds.y + 10;
    uint32_t visible_index = 0U;
    for (uint32_t index = 0U; index < surface.widget_count; ++index) {
        const ku_ui_widget& widget = surface.widgets[index];
        if ((widget.flags & KU_UI_WIDGET_HIDDEN) != 0U) continue;

        if (ku_ui_widget_has_absolute_layout(&widget)) {
            Rect rectangle{
                bounds.x + KU_UI_LAYOUT_X(widget.value),
                bounds.y + KU_UI_LAYOUT_Y(widget.value),
                KU_UI_LAYOUT_WIDTH(widget.maximum),
                KU_UI_LAYOUT_HEIGHT(widget.maximum)};
            draw_spatial_widget(
                widget,
                rectangle,
                focused,
                (widget.flags & KU_UI_WIDGET_SELECTED) != 0U ||
                    surface.selected_id == widget.id);
            continue;
        }

        // Progress keeps its value/maximum semantics. Mixed scenes can still
        // place live meters in a small legacy flow below/alongside spatial
        // cards until a future ABI adds an explicit rectangle for progress.
        if (visible_index++ < surface.scroll_offset) continue;
        if (surface.visible_rows != 0U &&
            visible_index > surface.scroll_offset + surface.visible_rows) break;
        const int32_t height = flow_height(widget.type);
        const Rect rectangle{
            bounds.x + 12, flow_y,
            bounds.width - 24, height - 4};
        if (widget.type == KU_UI_WIDGET_PROGRESS) {
            draw_progress_flow(widget, rectangle);
        } else {
            draw_spatial_widget(
                widget, rectangle, focused,
                (widget.flags & KU_UI_WIDGET_SELECTED) != 0U ||
                    surface.selected_id == widget.id);
        }
        flow_y += height;
    }
}

} // namespace ui

#include "ui_presenter.hpp"

#include "../drivers/framebuffer.hpp"

namespace user::ui_presenter {
namespace {

constexpr graphics::Color kSurface = UINT32_C(0x171C21);
constexpr graphics::Color kSurfaceRaised = UINT32_C(0x1F2429);
constexpr graphics::Color kSurfaceInset = UINT32_C(0x11161B);
constexpr graphics::Color kBorder = UINT32_C(0x31383F);
constexpr graphics::Color kMuted = UINT32_C(0x8D969F);
constexpr graphics::Color kSelected = UINT32_C(0x392225);
constexpr graphics::Color kShadow = UINT32_C(0x090D11);

bool geometry_valid(const ku_ui_line_style& style) {
    if (style.layout_width == 0 && style.layout_height == 0) {
        return style.corner_radius == 0U;
    }
    if (style.layout_width <= 0 || style.layout_height <= 0 ||
        style.layout_width > 4096 || style.layout_height > 4096 ||
        style.layout_x < -4096 || style.layout_x > 4096 ||
        style.layout_y < -4096 || style.layout_y > 4096 ||
        style.corner_radius > 48U ||
        style.corner_radius * 2U > static_cast<uint32_t>(style.layout_width) ||
        style.corner_radius * 2U > static_cast<uint32_t>(style.layout_height)) {
        return false;
    }
    return true;
}

uint32_t text_scale(const ku_text_style& text) {
    if (text.size_px <= 9U) return 1U;
    if (text.size_px <= 18U) return 2U;
    return 3U;
}

int32_t rounded_inset(int32_t y, int32_t height, int32_t radius) {
    if (radius <= 0) return 0;
    int32_t distance = 0;
    if (y < radius) distance = radius - 1 - y;
    else if (y >= height - radius) distance = y - (height - radius);
    else return 0;

    const int32_t radius_squared = radius * radius;
    int32_t horizontal = radius;
    while (horizontal > 0 &&
           horizontal * horizontal + distance * distance > radius_squared) {
        --horizontal;
    }
    return radius - horizontal;
}

void fill_rounded(const ui::Rect& bounds, uint32_t radius, graphics::Color color) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    int32_t r = static_cast<int32_t>(radius);
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
                bounds.x + inset, bounds.y + row, width, 1, color);
        }
    }
}

void rounded_surface(
    const ui::Rect& bounds,
    uint32_t radius,
    graphics::Color fill,
    graphics::Color border,
    bool shadow) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    if (shadow) {
        fill_rounded(
            {bounds.x + 3, bounds.y + 4, bounds.width, bounds.height},
            radius,
            kShadow);
    }
    fill_rounded(bounds, radius, border);
    if (bounds.width > 2 && bounds.height > 2) {
        const uint32_t inner_radius = radius > 1U ? radius - 1U : 0U;
        fill_rounded(
            {bounds.x + 1, bounds.y + 1, bounds.width - 2, bounds.height - 2},
            inner_radius,
            fill);
    }
}

void draw_text_in_bounds(
    const ui::Rect& bounds,
    const char* text,
    const ku_ui_line_style& style,
    graphics::Color foreground,
    graphics::Color background,
    int32_t horizontal_padding) {
    if (text == nullptr || text[0] == '\0') return;
    const uint32_t scale = text_scale(style.text);
    const int32_t glyph_height = static_cast<int32_t>(scale * 7U);
    int32_t y = bounds.y + (bounds.height - glyph_height) / 2;
    if (y < bounds.y) y = bounds.y;
    graphics::draw_text(
        bounds.x + horizontal_padding,
        y,
        text,
        foreground,
        background,
        scale,
        true);
}

void draw_explicit(
    const ku_ui_frame& frame,
    const ui::Rect& content,
    const char* text,
    const ku_ui_line_style& style) {
    ui::Rect bounds{
        content.x + style.layout_x,
        content.y + style.layout_y,
        style.layout_width,
        style.layout_height,
    };
    const bool inherit = (style.flags & KU_UI_LINE_STYLE_INHERIT_COLORS) != 0U;
    const bool selected = (style.flags & KU_UI_LINE_STYLE_SELECTED) != 0U;
    const bool muted = (style.flags & KU_UI_LINE_STYLE_MUTED) != 0U;
    const bool accented = (style.flags & KU_UI_LINE_STYLE_ACCENT) != 0U;
    const bool transparent =
        (style.flags & KU_UI_LINE_STYLE_TRANSPARENT_BACKGROUND) != 0U;
    graphics::Color foreground = inherit || style.foreground_rgb == 0U
        ? frame.foreground_rgb & UINT32_C(0xFFFFFF)
        : style.foreground_rgb & UINT32_C(0xFFFFFF);
    const graphics::Color accent = frame.accent_rgb & UINT32_C(0xFFFFFF);
    if (muted) foreground = kMuted;
    else if (accented) foreground = accent;

    graphics::Color fill = style.background_rgb == 0U
        ? kSurface
        : style.background_rgb & UINT32_C(0xFFFFFF);
    graphics::Color border = kBorder;
    if (selected) {
        fill = kSelected;
        border = accent;
    } else if (accented) {
        border = accent;
    }

    switch (style.visual_role) {
        case KU_UI_VISUAL_PANEL:
            if (!transparent) {
                rounded_surface(bounds, style.corner_radius, fill, border, true);
            }
            draw_text_in_bounds(bounds, text, style, foreground, fill, 12);
            break;
        case KU_UI_VISUAL_CARD:
            if (!transparent) {
                const graphics::Color card_fill = style.background_rgb == 0U
                    ? kSurfaceRaised : fill;
                rounded_surface(bounds, style.corner_radius, card_fill, border, true);
                fill = card_fill;
            }
            draw_text_in_bounds(bounds, text, style, foreground, fill, 14);
            break;
        case KU_UI_VISUAL_BUTTON:
        case KU_UI_VISUAL_LIST_ITEM:
            if (!transparent) {
                rounded_surface(bounds, style.corner_radius, fill, border, false);
                if (selected && bounds.height > 12) {
                    graphics::fill_rect(
                        bounds.x + 3,
                        bounds.y + 6,
                        3,
                        bounds.height - 12,
                        accent);
                }
            }
            draw_text_in_bounds(bounds, text, style, foreground, fill, 14);
            break;
        case KU_UI_VISUAL_INPUT:
            if (!transparent) {
                fill = style.background_rgb == 0U ? kSurfaceInset : fill;
                rounded_surface(bounds, style.corner_radius, fill, border, false);
            }
            draw_text_in_bounds(bounds, text, style, foreground, fill, 14);
            break;
        case KU_UI_VISUAL_SEPARATOR:
            graphics::fill_rect(
                bounds.x,
                bounds.y + bounds.height / 2,
                bounds.width,
                1,
                accented ? accent : border);
            break;
        case KU_UI_VISUAL_PROGRESS:
            if (text != nullptr && text[0] != '\0' && bounds.height >= 24) {
                draw_text_in_bounds(
                    {bounds.x, bounds.y, bounds.width, 18},
                    text,
                    style,
                    foreground,
                    frame.background_rgb,
                    0);
                ui::progress(
                    {bounds.x, bounds.y + 20, bounds.width, bounds.height - 20},
                    frame.progress_value,
                    frame.progress_maximum);
            } else {
                ui::progress(
                    bounds,
                    frame.progress_value,
                    frame.progress_maximum);
            }
            break;
        case KU_UI_VISUAL_TEXT:
        default:
            draw_text_in_bounds(
                bounds,
                text,
                style,
                foreground,
                frame.background_rgb,
                0);
            break;
    }
}

void draw_legacy(
    const ku_ui_frame& frame,
    const ui::Rect& content,
    bool focused) {
    const graphics::Color background =
        frame.background_rgb & UINT32_C(0xFFFFFF);
    const graphics::Color foreground =
        frame.foreground_rgb & UINT32_C(0xFFFFFF);
    const graphics::Color accent =
        frame.accent_rgb & UINT32_C(0xFFFFFF);
    int32_t y = content.y + 12;
    for (uint32_t index = 0U;
         index < frame.line_count && index < KU_UI_MAX_LINES;
         ++index) {
        const ku_ui_line_style& style = frame.line_styles[index];
        const bool inherit =
            (style.flags & KU_UI_LINE_STYLE_INHERIT_COLORS) != 0U;
        const bool transparent =
            (style.flags & KU_UI_LINE_STYLE_TRANSPARENT_BACKGROUND) != 0U;
        const uint32_t scale = text_scale(style.text);
        const graphics::Color line_background =
            inherit || style.background_rgb == 0U
                ? background
                : style.background_rgb & UINT32_C(0xFFFFFF);
        graphics::Color line_foreground =
            inherit || style.foreground_rgb == 0U
                ? foreground
                : style.foreground_rgb & UINT32_C(0xFFFFFF);
        if ((style.flags & KU_UI_LINE_STYLE_MUTED) != 0U) line_foreground = kMuted;
        else if ((style.flags & KU_UI_LINE_STYLE_ACCENT) != 0U) line_foreground = accent;
        else if (index == 0U && focused && inherit) line_foreground = accent;

        uint32_t requested_height = style.text.line_height_px != 0U
            ? style.text.line_height_px
            : scale * 8U + 6U;
        const uint32_t minimum_height = scale * 8U + 2U;
        if (requested_height < minimum_height) requested_height = minimum_height;
        if (requested_height > 48U) requested_height = 48U;
        const int32_t line_height = static_cast<int32_t>(requested_height);
        if (!transparent) {
            graphics::fill_rect(
                content.x + 6,
                y - 4,
                content.width > 12 ? content.width - 12 : content.width,
                line_height,
                line_background);
        }
        graphics::draw_text(
            content.x + 12,
            y,
            frame.lines[index],
            line_foreground,
            line_background,
            scale,
            true);
        y += line_height;
        if (y + static_cast<int32_t>(minimum_height) >=
            content.y + content.height) break;
    }
    if (frame.progress_maximum != 0U && content.height >= 70) {
        ui::progress(
            {content.x + 12, content.y + content.height - 32,
             content.width - 24, 16},
            frame.progress_value,
            frame.progress_maximum);
    }
}

} // namespace

bool style_valid(const ku_ui_line_style& style) {
    constexpr uint32_t known_flags =
        KU_UI_LINE_STYLE_INHERIT_COLORS |
        KU_UI_LINE_STYLE_TRANSPARENT_BACKGROUND |
        KU_UI_LINE_STYLE_DOCUMENT_CONTENT |
        KU_UI_LINE_STYLE_SELECTED |
        KU_UI_LINE_STYLE_MUTED |
        KU_UI_LINE_STYLE_ACCENT;
    return ku_text_style_valid(&style.text) &&
        style.reserved == 0U &&
        (style.flags & ~known_flags) == 0U &&
        style.visual_role <= KU_UI_VISUAL_CARD &&
        geometry_valid(style);
}

void draw_frame(
    const ku_ui_frame& frame,
    const ui::Rect& content,
    bool focused) {
    const graphics::Color background =
        frame.background_rgb & UINT32_C(0xFFFFFF);
    graphics::fill_rect(
        content.x,
        content.y,
        content.width,
        content.height,
        background);

    bool has_explicit_geometry = false;
    for (uint32_t index = 0U;
         index < frame.line_count && index < KU_UI_MAX_LINES;
         ++index) {
        if (frame.line_styles[index].layout_width > 0 &&
            frame.line_styles[index].layout_height > 0) {
            has_explicit_geometry = true;
            break;
        }
    }
    if (!has_explicit_geometry) {
        draw_legacy(frame, content, focused);
        return;
    }

    for (uint32_t index = 0U;
         index < frame.line_count && index < KU_UI_MAX_LINES;
         ++index) {
        const ku_ui_line_style& style = frame.line_styles[index];
        if (style.layout_width > 0 && style.layout_height > 0) {
            draw_explicit(frame, content, frame.lines[index], style);
        }
    }
}

} // namespace user::ui_presenter

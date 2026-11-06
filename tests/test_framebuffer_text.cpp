#include "../kernel/drivers/framebuffer.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {
constexpr uint32_t WIDTH = 320U;
constexpr uint32_t HEIGHT = 96U;
uint32_t g_pixels[WIDTH * HEIGHT]{};

uint32_t at(uint32_t x, uint32_t y) {
    return g_pixels[static_cast<size_t>(y) * WIDTH + x];
}

size_t count_nonblack(uint32_t x, uint32_t y, uint32_t width,
                      uint32_t height) {
    size_t count = 0U;
    for (uint32_t row = y; row < y + height; ++row) {
        for (uint32_t column = x; column < x + width; ++column) {
            if (at(column, row) != 0U) ++count;
        }
    }
    return count;
}

size_t count_gray(uint32_t x, uint32_t y, uint32_t width,
                  uint32_t height) {
    size_t count = 0U;
    for (uint32_t row = y; row < y + height; ++row) {
        for (uint32_t column = x; column < x + width; ++column) {
            const uint32_t pixel = at(column, row) & UINT32_C(0x00ffffff);
            if (pixel != 0U && pixel != UINT32_C(0x00ffffff)) ++count;
        }
    }
    return count;
}
}

int main() {
    KuroganeFramebuffer framebuffer{};
    framebuffer.base = g_pixels;
    framebuffer.width = WIDTH;
    framebuffer.height = HEIGHT;
    framebuffer.pitch = WIDTH * sizeof(uint32_t);
    framebuffer.bpp = 32U;
    framebuffer.pixel_format = KUROGANE_PIXEL_BGRX8;
    assert(graphics::init(framebuffer));

    assert(graphics::text_scale_percent() == 100U);
    assert(!graphics::set_text_scale_percent(110U));
    assert(graphics::text_scale_percent() == 100U);
    assert(graphics::set_text_scale_percent(100U));
    const uint32_t width_100 = graphics::measure_text_width("Kurogane");
    assert(graphics::set_text_scale_percent(125U));
    const uint32_t width_125 = graphics::measure_text_width("Kurogane");
    assert(graphics::set_text_scale_percent(150U));
    const uint32_t width_150 = graphics::measure_text_width("Kurogane");
    assert(graphics::set_text_scale_percent(200U));
    const uint32_t width_200 = graphics::measure_text_width("Kurogane");
    assert(width_100 < width_125);
    assert(width_125 < width_150);
    assert(width_150 < width_200);
    int32_t scaled_coordinate = 0;
    int32_t logical_coordinate = 0;
    assert(graphics::scale_layout_coordinate(
        80, 100U, &scaled_coordinate) && scaled_coordinate == 80);
    assert(graphics::unscale_layout_coordinate(
        scaled_coordinate, 100U, &logical_coordinate) &&
        logical_coordinate == 80);
    assert(graphics::scale_layout_coordinate(
        80, 125U, &scaled_coordinate) && scaled_coordinate == 100);
    assert(graphics::unscale_layout_coordinate(
        scaled_coordinate, 125U, &logical_coordinate) &&
        logical_coordinate == 80);
    assert(graphics::scale_layout_coordinate(
        -80, 150U, &scaled_coordinate) && scaled_coordinate == -120);
    assert(graphics::unscale_layout_coordinate(
        scaled_coordinate, 150U, &logical_coordinate) &&
        logical_coordinate == -80);
    assert(graphics::scale_layout_coordinate(
        80, 200U, &scaled_coordinate) && scaled_coordinate == 160);
    assert(graphics::unscale_layout_coordinate(
        scaled_coordinate, 200U, &logical_coordinate) &&
        logical_coordinate == 80);
    assert(!graphics::scale_layout_coordinate(
        80, 110U, &scaled_coordinate));
    assert(!graphics::unscale_layout_coordinate(
        80, 110U, &logical_coordinate));
    assert(!graphics::scale_layout_coordinate(80, 100U, nullptr));
    assert(graphics::scale_layout_coordinate(
        INT32_MAX, 200U, &scaled_coordinate) &&
        scaled_coordinate == INT32_MAX);
    char kerned_pairs[201]{};
    for (size_t pair = 0U; pair < 100U; ++pair) {
        kerned_pairs[pair * 2U] = 'A';
        kerned_pairs[pair * 2U + 1U] = 'V';
    }
    assert(graphics::measure_text_width(kerned_pairs, 2U) <
           graphics::measure_text_width("AV", 2U) * 100U);

    assert(graphics::set_text_scale_percent(100U));
    assert(graphics::measure_text_width("iiii") <
           graphics::measure_text_width("WWWW"));
    graphics::clear(0U);
    graphics::draw_text(4, 4, "ą ć ę ł ń ó ś ź ż Ą Ć Ę Ł Ń Ó Ś Ź Ż",
                        UINT32_C(0x00ffffff), 0U, 1U, true);
    assert(count_nonblack(0U, 0U, WIDTH, HEIGHT) > 0U);
    assert(count_gray(0U, 0U, WIDTH, HEIGHT) > 0U);
    const char* polish_letters[] = {
        "ą", "ć", "ę", "ł", "ń", "ó", "ś", "ź", "ż",
        "Ą", "Ć", "Ę", "Ł", "Ń", "Ó", "Ś", "Ź", "Ż",
    };
    for (const char* character : polish_letters) {
        assert(graphics::measure_text_width(character) != 0U);
    }
    assert(graphics::measure_text_width("\xf0\x9f\x90\x88") ==
           graphics::measure_text_width("?"));
    assert(graphics::measure_text_width("\xff") ==
           graphics::measure_text_width("\xef\xbf\xbd"));

    graphics::clear(0U);
    graphics::draw_text_styled(4, 36, "Kurogane", UINT32_C(0x00ffffff),
                               0U, 1U, true, graphics::TextWeight::Normal);
    uint32_t normal[WIDTH * 24U]{};
    for (uint32_t row = 0U; row < 24U; ++row) {
        for (uint32_t column = 0U; column < WIDTH; ++column) {
            normal[static_cast<size_t>(row) * WIDTH + column] =
                at(column, row + 32U);
        }
    }
    graphics::draw_text_styled(4, 64, "Kurogane", UINT32_C(0x00ffffff),
                               0U, 1U, true, graphics::TextWeight::Bold);
    bool weight_differs = false;
    for (uint32_t row = 0U; row < 24U; ++row) {
        for (uint32_t column = 0U; column < WIDTH; ++column) {
            if (normal[static_cast<size_t>(row) * WIDTH + column] !=
                at(column, row + 60U)) {
                weight_differs = true;
            }
        }
    }
    assert(weight_differs);

    graphics::clear(0U);
    const char* title = "ZAŻÓŁĆ";
    constexpr uint32_t title_limit = 20U;
    const uint32_t rendered_title_width = graphics::draw_text_ellipsized(
        4, 4, title, title_limit, UINT32_C(0x00ffffff), 0U, 1U, true,
        graphics::TextWeight::Bold);
    assert(graphics::measure_text_width(title, 1U, graphics::TextWeight::Bold) >
           title_limit);
    assert(rendered_title_width <= title_limit);
    const uint32_t ellipsis_width = graphics::measure_text_width(
        "...", 1U, graphics::TextWeight::Bold);
    const char* complete_prefixes[] = {
        "", "Z", "ZA", "ZAŻ", "ZAŻÓ", "ZAŻÓŁ", "ZAŻÓŁĆ",
    };
    bool ends_at_codepoint_boundary = false;
    for (const char* prefix : complete_prefixes) {
        if (rendered_title_width ==
            graphics::measure_text_width(prefix, 1U, graphics::TextWeight::Bold) +
                ellipsis_width) {
            ends_at_codepoint_boundary = true;
        }
    }
    assert(ends_at_codepoint_boundary);
    for (uint32_t row = 0U; row < HEIGHT; ++row) {
        for (uint32_t column = 0U; column < WIDTH; ++column) {
            if (column >= 4U &&
                column < 4U + rendered_title_width &&
                row >= 4U && row < 20U) {
                continue;
            }
            assert(at(column, row) == 0U);
        }
    }
    assert(graphics::draw_text_ellipsized(
        4, 4, title, 1U, UINT32_C(0x00ffffff), 0U, 1U, true,
        graphics::TextWeight::Bold) == 0U);

    graphics::clear(0U);
    const uint32_t wrapped_lines = graphics::draw_text_wrapped(
        4, 4, "alpha beta gamma delta", 35U, 40U,
        UINT32_C(0x00ffffff), 0U, 1U, true, graphics::TextWeight::Normal);
    assert(wrapped_lines >= 3U);
    for (uint32_t row = 0U; row < HEIGHT; ++row) {
        for (uint32_t column = 0U; column < WIDTH; ++column) {
            if (column >= 4U && column < 39U &&
                row >= 4U && row < 44U) {
                continue;
            }
            assert(at(column, row) == 0U);
        }
    }
    graphics::clear(0U);
    assert(graphics::draw_text_wrapped(
        4, 4, "alpha beta gamma delta", 35U, 10U,
        UINT32_C(0x00ffffff), 0U, 1U, true, graphics::TextWeight::Normal) == 1U);

    graphics::clear(0U);
    graphics::set_clip(24, 10, 32, 18);
    graphics::draw_text(18, 8, "Text", UINT32_C(0x00ffffff), 0U, 1U, true);
    graphics::reset_clip();
    assert(count_nonblack(24U, 10U, 32U, 18U) > 0U);
    for (uint32_t row = 0U; row < HEIGHT; ++row) {
        for (uint32_t column = 0U; column < WIDTH; ++column) {
            if (column >= 24U && column < 56U &&
                row >= 10U && row < 28U) {
                continue;
            }
            assert(at(column, row) == 0U);
        }
    }

    graphics::set_text_scale_percent(100U);
    const uint32_t default_width = graphics::measure_text_width("KOS", 1U);
    graphics::set_text_scale_limit(1U);
    assert(graphics::measure_text_width("KOS", 2U) == default_width);
    graphics::reset_text_scale_limit();
    std::puts("framebuffer scalable text, UTF-8, antialias and weight tests passed");
    return 0;
}

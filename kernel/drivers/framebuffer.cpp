#include "framebuffer.hpp"

#include "../core/memory.hpp"

namespace graphics {

namespace {
KuroganeFramebuffer g_framebuffer{};
bool g_available = false;

struct Glyph {
    char character;
    uint8_t rows[7];
};

/*
 * Zwarty font 5x7. Małe litery celowo używają glifów wielkich liter:
 * tekst pozostaje czytelny, a tabela nie obciąża małego kernela.
 */
constexpr Glyph kGlyphs[] = {
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}},
    {'6', {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E}},
    {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
    {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06}},
    {',', {0x00, 0x00, 0x00, 0x00, 0x06, 0x06, 0x04}},
    {':', {0x00, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00}},
    {';', {0x00, 0x06, 0x06, 0x00, 0x06, 0x06, 0x04}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {'=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}},
    {'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}},
    {'\\', {0x10, 0x08, 0x08, 0x04, 0x02, 0x02, 0x01}},
    {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
    {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {'[', {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}},
    {']', {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}},
    {'<', {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}},
    {'>', {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}},
    {'\'', {0x04, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00}},
    {'"', {0x0A, 0x0A, 0x05, 0x00, 0x00, 0x00, 0x00}},
    {'@', {0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E}},
    {'#', {0x0A, 0x1F, 0x0A, 0x0A, 0x1F, 0x0A, 0x00}},
    {'$', {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04}},
    {'%', {0x19, 0x1A, 0x02, 0x04, 0x08, 0x0B, 0x13}},
    {'&', {0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D}},
    {'*', {0x00, 0x15, 0x0E, 0x1F, 0x0E, 0x15, 0x00}},
    {'|', {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'^', {0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00}},
    {'~', {0x00, 0x00, 0x09, 0x16, 0x00, 0x00, 0x00}},
};

uint32_t native_color(Color color) {
    if (g_framebuffer.pixel_format == KUROGANE_PIXEL_RGBX8) {
        return ((color & 0x0000FFu) << 16) |
               (color & 0x00FF00u) |
               ((color & 0xFF0000u) >> 16);
    }
    return color;
}

const uint8_t* glyph_rows(char character) {
    if (character >= 'a' && character <= 'z') {
        character = static_cast<char>(character - ('a' - 'A'));
    }
    for (const auto& glyph : kGlyphs) {
        if (glyph.character == character) {
            return glyph.rows;
        }
    }
    return nullptr;
}

int32_t saturate_i32(int64_t value) {
    if (value < INT32_MIN) {
        return INT32_MIN;
    }
    if (value > INT32_MAX) {
        return INT32_MAX;
    }
    return static_cast<int32_t>(value);
}
} // namespace

bool init(const KuroganeFramebuffer& framebuffer) {
    g_available = false;
    if (!framebuffer.base || framebuffer.width == 0 ||
        framebuffer.height == 0 ||
        framebuffer.width > static_cast<uint32_t>(INT32_MAX) ||
        framebuffer.height > static_cast<uint32_t>(INT32_MAX) ||
        framebuffer.width > UINT32_MAX / 4 ||
        framebuffer.bpp != 32 ||
        (framebuffer.pitch & 3u) != 0 ||
        framebuffer.pitch < framebuffer.width * 4 ||
        (framebuffer.pixel_format != KUROGANE_PIXEL_BGRX8 &&
         framebuffer.pixel_format != KUROGANE_PIXEL_RGBX8)) {
        return false;
    }
    g_framebuffer = framebuffer;
    g_available = true;
    return true;
}

bool available() {
    return g_available;
}

const KuroganeFramebuffer& info() {
    return g_framebuffer;
}

uint32_t width() {
    return g_framebuffer.width;
}

uint32_t height() {
    return g_framebuffer.height;
}

void put_pixel(int32_t x, int32_t y, Color color) {
    if (!g_available || x < 0 || y < 0 ||
        x >= static_cast<int32_t>(g_framebuffer.width) ||
        y >= static_cast<int32_t>(g_framebuffer.height)) {
        return;
    }
    auto* row = reinterpret_cast<uint32_t*>(
        reinterpret_cast<uint8_t*>(g_framebuffer.base) +
        static_cast<size_t>(y) * g_framebuffer.pitch);
    row[x] = native_color(color);
}

void clear(Color color) {
    fill_rect(0, 0, static_cast<int32_t>(g_framebuffer.width),
              static_cast<int32_t>(g_framebuffer.height), color);
}

void fill_rect(int32_t x, int32_t y, int32_t rectangle_width,
               int32_t rectangle_height, Color color) {
    if (!g_available || rectangle_width <= 0 || rectangle_height <= 0) {
        return;
    }
    const int64_t framebuffer_width =
        static_cast<int64_t>(g_framebuffer.width);
    const int64_t framebuffer_height =
        static_cast<int64_t>(g_framebuffer.height);
    const int64_t unclipped_right =
        static_cast<int64_t>(x) + rectangle_width;
    const int64_t unclipped_bottom =
        static_cast<int64_t>(y) + rectangle_height;
    const int64_t clipped_left = x < 0 ? 0 : x;
    const int64_t clipped_top = y < 0 ? 0 : y;
    const int64_t clipped_right =
        unclipped_right < framebuffer_width
            ? unclipped_right
            : framebuffer_width;
    const int64_t clipped_bottom =
        unclipped_bottom < framebuffer_height
            ? unclipped_bottom
            : framebuffer_height;
    if (clipped_left >= clipped_right || clipped_top >= clipped_bottom) {
        return;
    }
    const int32_t left = static_cast<int32_t>(clipped_left);
    const int32_t top = static_cast<int32_t>(clipped_top);
    const int32_t right = static_cast<int32_t>(clipped_right);
    const int32_t bottom = static_cast<int32_t>(clipped_bottom);

    const uint32_t native = native_color(color);
    for (int32_t py = top; py < bottom; ++py) {
        auto* row = reinterpret_cast<uint32_t*>(
            reinterpret_cast<uint8_t*>(g_framebuffer.base) +
            static_cast<size_t>(py) * g_framebuffer.pitch);
        for (int32_t px = left; px < right; ++px) {
            row[px] = native;
        }
    }
}

void draw_rect(int32_t x, int32_t y, int32_t rectangle_width,
               int32_t rectangle_height, Color color, uint32_t thickness) {
    if (thickness == 0) {
        return;
    }
    const int32_t line =
        thickness > static_cast<uint32_t>(INT32_MAX)
            ? INT32_MAX
            : static_cast<int32_t>(thickness);
    fill_rect(x, y, rectangle_width, line, color);
    fill_rect(x, saturate_i32(
                     static_cast<int64_t>(y) + rectangle_height - line),
              rectangle_width, line, color);
    fill_rect(x, y, line, rectangle_height, color);
    fill_rect(saturate_i32(
                  static_cast<int64_t>(x) + rectangle_width - line),
              y, line, rectangle_height, color);
}

void draw_char(int32_t x, int32_t y, char character, Color foreground,
               Color background, uint32_t scale, bool transparent) {
    if (!g_available || scale == 0) {
        return;
    }
    const int32_t pixel_size =
        scale > static_cast<uint32_t>(INT32_MAX)
            ? INT32_MAX
            : static_cast<int32_t>(scale);
    const uint8_t* rows = glyph_rows(character);
    for (uint32_t row = 0; row < 7; ++row) {
        for (uint32_t column = 0; column < 5; ++column) {
            const bool set = rows && (rows[row] & (1u << (4u - column)));
            if (set || !transparent) {
                fill_rect(saturate_i32(
                              static_cast<int64_t>(x) +
                              static_cast<int64_t>(column) * scale),
                          saturate_i32(
                              static_cast<int64_t>(y) +
                              static_cast<int64_t>(row) * scale),
                          pixel_size, pixel_size,
                          set ? foreground : background);
            }
        }
        if (!transparent) {
            fill_rect(saturate_i32(
                          static_cast<int64_t>(x) +
                          INT64_C(5) * scale),
                      saturate_i32(
                          static_cast<int64_t>(y) +
                          static_cast<int64_t>(row) * scale),
                      pixel_size, pixel_size, background);
        }
    }
    if (!transparent) {
        const uint64_t cell_width = UINT64_C(6) * scale;
        fill_rect(x, saturate_i32(
                         static_cast<int64_t>(y) + INT64_C(7) * scale),
                  cell_width > static_cast<uint64_t>(INT32_MAX)
                      ? INT32_MAX
                      : static_cast<int32_t>(cell_width),
                  pixel_size, background);
    }
}

void draw_text(int32_t x, int32_t y, const char* text, Color foreground,
               Color background, uint32_t scale, bool transparent) {
    if (!text || scale == 0) {
        return;
    }
    int64_t cursor_x = x;
    int64_t cursor_y = y;
    const int64_t line_advance = static_cast<uint64_t>(scale) * 8u;
    const int64_t column_advance = static_cast<uint64_t>(scale) * 6u;
    while (*text) {
        if (*text == '\n') {
            cursor_x = x;
            cursor_y =
                cursor_y > INT64_MAX - line_advance
                    ? INT64_MAX
                    : cursor_y + line_advance;
        } else {
            draw_char(saturate_i32(cursor_x), saturate_i32(cursor_y),
                      *text, foreground, background, scale, transparent);
            cursor_x =
                cursor_x > INT64_MAX - column_advance
                    ? INT64_MAX
                    : cursor_x + column_advance;
        }
        ++text;
    }
}

void scroll_up(uint32_t pixels, Color fill) {
    if (!g_available || pixels == 0) {
        return;
    }
    if (pixels >= g_framebuffer.height) {
        clear(fill);
        return;
    }
    const size_t bytes_to_move =
        static_cast<size_t>(g_framebuffer.height - pixels) *
        g_framebuffer.pitch;
    auto* base = reinterpret_cast<uint8_t*>(g_framebuffer.base);
    memmove(base, base + static_cast<size_t>(pixels) * g_framebuffer.pitch,
            bytes_to_move);
    fill_rect(0, static_cast<int32_t>(g_framebuffer.height - pixels),
              static_cast<int32_t>(g_framebuffer.width),
              static_cast<int32_t>(pixels), fill);
}

} // namespace graphics

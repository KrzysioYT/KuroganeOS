#include "framebuffer.hpp"
#include "font_atlas.generated.hpp"
#include "rounded_rect.hpp"
#include "../libk/utf8_text.hpp"

#include "../core/memory.hpp"

namespace graphics {

namespace {
static_assert(font_atlas::kCoverage4DecodedSize <= 512U * 1024U);

KuroganeFramebuffer g_framebuffer{};
bool g_available = false;

// Kurogane 3.1 software compositor surface. 1600x1200 covers the current QEMU
// development GOP modes while avoiding dependence on the small kernel heap.
constexpr uint32_t kBackbufferWidth = 1600U;
constexpr uint32_t kBackbufferHeight = 1200U;
constexpr size_t kBackbufferPixels =
    static_cast<size_t>(kBackbufferWidth) * kBackbufferHeight;
alignas(64) static uint32_t g_backbuffer[kBackbufferPixels];
bool g_frame_active = false;

struct ClipState {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
    bool enabled;
};

ClipState g_clip{};
DamageRect g_damage_regions[MAX_COMPOSITOR_DAMAGE_REGIONS]{};
size_t g_damage_count = 0U;
bool g_damage_active = false;
uint32_t g_text_scale_limit = UINT32_MAX;
uint32_t g_text_scale_percent = 100U;
uint8_t g_font_coverage4[font_atlas::kCoverage4DecodedSize]{};

struct Glyph {
    char character;
    uint8_t rows[7];
};

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
        if (glyph.character == character) return glyph.rows;
    }
    return nullptr;
}

int32_t saturate_i32(int64_t value) {
    if (value < INT32_MIN) return INT32_MIN;
    if (value > INT32_MAX) return INT32_MAX;
    return static_cast<int32_t>(value);
}

int base64_value(char character) {
    if (character >= 'A' && character <= 'Z') return character - 'A';
    if (character >= 'a' && character <= 'z') return character - 'a' + 26;
    if (character >= '0' && character <= '9') return character - '0' + 52;
    if (character == '+') return 62;
    if (character == '/') return 63;
    return -1;
}

bool decode_font_coverage() {
    uint32_t accumulator = 0U;
    uint32_t bits = 0U;
    size_t produced = 0U;
    for (size_t index = 0U; font_atlas::kCoverage4Base64[index] != '\0'; ++index) {
        const char character = font_atlas::kCoverage4Base64[index];
        if (character == '=') break;
        const int value = base64_value(character);
        if (value < 0) return false;
        accumulator = (accumulator << 6U) | static_cast<uint32_t>(value);
        bits += 6U;
        if (bits >= 8U) {
            bits -= 8U;
            if (produced >= sizeof(g_font_coverage4)) return false;
            g_font_coverage4[produced++] = static_cast<uint8_t>(
                (accumulator >> bits) & UINT32_C(0xff));
        }
    }
    return produced == sizeof(g_font_coverage4);
}

size_t glyph_index_fallback() {
    size_t first = 0U;
    size_t last = font_atlas::kGlyphCount;
    while (first < last) {
        const size_t middle = first + (last - first) / 2U;
        if (font_atlas::kCodepoints[middle] < static_cast<uint32_t>('?')) {
            first = middle + 1U;
        } else {
            last = middle;
        }
    }
    return first;
}

size_t glyph_index(uint32_t codepoint) {
    size_t first = 0U;
    size_t last = font_atlas::kGlyphCount;
    while (first < last) {
        const size_t middle = first + (last - first) / 2U;
        if (font_atlas::kCodepoints[middle] < codepoint) {
            first = middle + 1U;
        } else {
            last = middle;
        }
    }
    return first < font_atlas::kGlyphCount &&
            font_atlas::kCodepoints[first] == codepoint
        ? first
        : glyph_index_fallback();
}

size_t font_size_index(uint32_t scale) {
    const uint64_t requested =
        (static_cast<uint64_t>(scale) * 8U * g_text_scale_percent + 50U) / 100U;
    size_t best = 0U;
    uint64_t best_distance = UINT64_MAX;
    for (size_t index = 0U; index < font_atlas::kSizeCount; ++index) {
        const uint64_t candidate = font_atlas::kSizes[index];
        const uint64_t distance = requested > candidate
            ? requested - candidate
            : candidate - requested;
        if (distance < best_distance) {
            best = index;
            best_distance = distance;
        }
    }
    return best;
}

size_t metrics_index(size_t size_index, size_t glyph, TextWeight weight) {
    const size_t weight_offset = weight == TextWeight::Bold
        ? font_atlas::kSizeCount * font_atlas::kGlyphCount
        : 0U;
    return weight_offset + size_index * font_atlas::kGlyphCount + glyph;
}

uint8_t* draw_base() {
    return g_frame_active
        ? reinterpret_cast<uint8_t*>(g_backbuffer)
        : reinterpret_cast<uint8_t*>(g_framebuffer.base);
}

size_t draw_pitch() {
    return g_frame_active
        ? static_cast<size_t>(g_framebuffer.width) * sizeof(uint32_t)
        : g_framebuffer.pitch;
}

uint32_t effective_scale(uint32_t requested) {
    if (requested == 0U) return 0U;
    return requested > g_text_scale_limit ? g_text_scale_limit : requested;
}

bool clip_damage_rect(const DamageRect& input, DamageRect* output) {
    if (output == nullptr || input.width <= 0 || input.height <= 0) return false;
    int64_t left = input.x;
    int64_t top = input.y;
    int64_t right = static_cast<int64_t>(input.x) + input.width;
    int64_t bottom = static_cast<int64_t>(input.y) + input.height;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > static_cast<int64_t>(g_framebuffer.width)) right = g_framebuffer.width;
    if (bottom > static_cast<int64_t>(g_framebuffer.height)) bottom = g_framebuffer.height;
    if (left >= right || top >= bottom) return false;
    *output = {
        static_cast<int32_t>(left), static_cast<int32_t>(top),
        static_cast<int32_t>(right - left),
        static_cast<int32_t>(bottom - top),
    };
    return true;
}

bool damage_contains(int32_t x, int32_t y) {
    if (!g_damage_active) return true;
    for (size_t index = 0U; index < g_damage_count; ++index) {
        const DamageRect& region = g_damage_regions[index];
        if (x >= region.x && y >= region.y &&
            x < region.x + region.width && y < region.y + region.height) {
            return true;
        }
    }
    return false;
}

void clip_edges(
    int64_t& left, int64_t& top, int64_t& right, int64_t& bottom) {
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > static_cast<int64_t>(g_framebuffer.width)) {
        right = static_cast<int64_t>(g_framebuffer.width);
    }
    if (bottom > static_cast<int64_t>(g_framebuffer.height)) {
        bottom = static_cast<int64_t>(g_framebuffer.height);
    }
    if (g_clip.enabled) {
        if (left < g_clip.left) left = g_clip.left;
        if (top < g_clip.top) top = g_clip.top;
        if (right > g_clip.right) right = g_clip.right;
        if (bottom > g_clip.bottom) bottom = g_clip.bottom;
    }
}
} // namespace

bool init(const KuroganeFramebuffer& framebuffer) {
    g_available = false;
    g_frame_active = false;
    g_clip = {};
    g_damage_count = 0U;
    g_damage_active = false;
    g_text_scale_limit = UINT32_MAX;
    g_text_scale_percent = 100U;
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
    if (!decode_font_coverage()) return false;
    g_framebuffer = framebuffer;
    g_available = true;
    return true;
}

bool available() { return g_available; }
const KuroganeFramebuffer& info() { return g_framebuffer; }
uint32_t width() { return g_framebuffer.width; }
uint32_t height() { return g_framebuffer.height; }

bool begin_frame() {
    if (!g_available || g_frame_active ||
        g_framebuffer.width > kBackbufferWidth ||
        g_framebuffer.height > kBackbufferHeight) {
        return false;
    }
    g_frame_active = true;
    reset_damage_regions();
    reset_clip();
    reset_text_scale_limit();
    return true;
}

void end_frame_regions(const DamageRect* regions, size_t count) {
    if (!g_available || !g_frame_active) return;

    if (regions != nullptr) {
        auto* framebuffer_bytes = reinterpret_cast<uint8_t*>(g_framebuffer.base);
        const uint32_t frame_width = g_framebuffer.width;
        const uint32_t frame_height = g_framebuffer.height;
        for (size_t region_index = 0U; region_index < count; ++region_index) {
            const DamageRect& region = regions[region_index];
            if (region.width <= 0 || region.height <= 0) continue;
            int64_t left = region.x;
            int64_t top = region.y;
            int64_t right = static_cast<int64_t>(region.x) + region.width;
            int64_t bottom = static_cast<int64_t>(region.y) + region.height;
            if (left < 0) left = 0;
            if (top < 0) top = 0;
            if (right > frame_width) right = frame_width;
            if (bottom > frame_height) bottom = frame_height;
            if (left >= right || top >= bottom) continue;

            const uint32_t x_begin = static_cast<uint32_t>(left);
            const uint32_t x_end = static_cast<uint32_t>(right);
            for (uint32_t y = static_cast<uint32_t>(top);
                 y < static_cast<uint32_t>(bottom); ++y) {
                auto* destination_row = reinterpret_cast<uint32_t*>(
                    framebuffer_bytes + static_cast<size_t>(y) * g_framebuffer.pitch);
                const auto* source_row = g_backbuffer +
                    static_cast<size_t>(y) * static_cast<size_t>(frame_width);

                uint32_t first_changed = x_begin;
                while (first_changed < x_end &&
                       destination_row[first_changed] == source_row[first_changed]) {
                    ++first_changed;
                }
                if (first_changed == x_end) continue;

                uint32_t last_changed = x_end;
                while (last_changed > first_changed &&
                       destination_row[last_changed - 1U] ==
                           source_row[last_changed - 1U]) {
                    --last_changed;
                }
                const size_t changed_pixels = static_cast<size_t>(
                    last_changed - first_changed);
                memcpy(
                    destination_row + first_changed,
                    source_row + first_changed,
                    changed_pixels * sizeof(uint32_t));
            }
        }
    }

    g_frame_active = false;
    reset_damage_regions();
    reset_clip();
    reset_text_scale_limit();
}

void end_frame() {
    if (!g_available || !g_frame_active) return;
    const DamageRect full = {
        0, 0,
        static_cast<int32_t>(g_framebuffer.width),
        static_cast<int32_t>(g_framebuffer.height),
    };
    end_frame_regions(&full, 1U);
}

bool frame_active() { return g_frame_active; }

bool set_damage_regions(const DamageRect* regions, size_t count) {
    reset_damage_regions();
    if (!g_available || regions == nullptr || count == 0U ||
        count > MAX_COMPOSITOR_DAMAGE_REGIONS) return false;
    for (size_t index = 0U; index < count; ++index) {
        DamageRect clipped{};
        if (!clip_damage_rect(regions[index], &clipped)) continue;
        g_damage_regions[g_damage_count++] = clipped;
    }
    g_damage_active = g_damage_count != 0U;
    return g_damage_active;
}

void reset_damage_regions() {
    g_damage_count = 0U;
    g_damage_active = false;
}

bool damage_regions_active() { return g_damage_active; }

void set_clip(int32_t x, int32_t y, int32_t rectangle_width, int32_t rectangle_height) {
    int64_t left = x;
    int64_t top = y;
    int64_t right = static_cast<int64_t>(x) + rectangle_width;
    int64_t bottom = static_cast<int64_t>(y) + rectangle_height;
    if (rectangle_width <= 0 || rectangle_height <= 0) {
        g_clip = {0, 0, 0, 0, true};
        return;
    }
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > static_cast<int64_t>(g_framebuffer.width)) right = g_framebuffer.width;
    if (bottom > static_cast<int64_t>(g_framebuffer.height)) bottom = g_framebuffer.height;
    if (right < left) right = left;
    if (bottom < top) bottom = top;
    g_clip = {
        saturate_i32(left), saturate_i32(top),
        saturate_i32(right), saturate_i32(bottom), true};
}

void reset_clip() { g_clip = {}; }

void set_text_scale_limit(uint32_t maximum_scale) {
    g_text_scale_limit = maximum_scale == 0U ? 1U : maximum_scale;
}

void reset_text_scale_limit() { g_text_scale_limit = UINT32_MAX; }

bool set_text_scale_percent(uint32_t percent) {
    if (percent != 100U && percent != 125U &&
        percent != 150U && percent != 200U) {
        return false;
    }
    g_text_scale_percent = percent;
    return true;
}

uint32_t text_scale_percent() { return g_text_scale_percent; }

void put_pixel(int32_t x, int32_t y, Color color) {
    if (!g_available || x < 0 || y < 0 ||
        x >= static_cast<int32_t>(g_framebuffer.width) ||
        y >= static_cast<int32_t>(g_framebuffer.height)) return;
    if (g_clip.enabled &&
        (x < g_clip.left || x >= g_clip.right ||
         y < g_clip.top || y >= g_clip.bottom)) return;
    if (!damage_contains(x, y)) return;
    auto* row = reinterpret_cast<uint32_t*>(
        draw_base() + static_cast<size_t>(y) * draw_pitch());
    row[x] = native_color(color);
}

void clear(Color color) {
    reset_clip();
    fill_rect(0, 0, static_cast<int32_t>(g_framebuffer.width),
              static_cast<int32_t>(g_framebuffer.height), color);
}

void fill_rect(int32_t x, int32_t y, int32_t rectangle_width,
               int32_t rectangle_height, Color color) {
    if (!g_available || rectangle_width <= 0 || rectangle_height <= 0) return;
    int64_t left = x;
    int64_t top = y;
    int64_t right = static_cast<int64_t>(x) + rectangle_width;
    int64_t bottom = static_cast<int64_t>(y) + rectangle_height;
    clip_edges(left, top, right, bottom);
    if (left >= right || top >= bottom) return;

    const uint32_t native = native_color(color);
    const auto paint = [native](int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
        for (int32_t py = y1; py < y2; ++py) {
            auto* row = reinterpret_cast<uint32_t*>(
                draw_base() + static_cast<size_t>(py) * draw_pitch());
            for (int32_t px = x1; px < x2; ++px) row[px] = native;
        }
    };
    if (!g_damage_active) {
        paint(static_cast<int32_t>(left), static_cast<int32_t>(top),
              static_cast<int32_t>(right), static_cast<int32_t>(bottom));
        return;
    }

    for (size_t index = 0U; index < g_damage_count; ++index) {
        const DamageRect& damage = g_damage_regions[index];
        int64_t clipped_left = left > damage.x ? left : damage.x;
        int64_t clipped_top = top > damage.y ? top : damage.y;
        const int64_t damage_right = static_cast<int64_t>(damage.x) + damage.width;
        const int64_t damage_bottom = static_cast<int64_t>(damage.y) + damage.height;
        int64_t clipped_right = right < damage_right ? right : damage_right;
        int64_t clipped_bottom = bottom < damage_bottom ? bottom : damage_bottom;
        if (clipped_left >= clipped_right || clipped_top >= clipped_bottom) continue;
        paint(static_cast<int32_t>(clipped_left), static_cast<int32_t>(clipped_top),
              static_cast<int32_t>(clipped_right), static_cast<int32_t>(clipped_bottom));
    }
}

void draw_rect(int32_t x, int32_t y, int32_t rectangle_width,
               int32_t rectangle_height, Color color, uint32_t thickness) {
    if (thickness == 0U) return;
    const int32_t line =
        thickness > static_cast<uint32_t>(INT32_MAX)
            ? INT32_MAX : static_cast<int32_t>(thickness);
    fill_rect(x, y, rectangle_width, line, color);
    fill_rect(x, saturate_i32(static_cast<int64_t>(y) + rectangle_height - line),
              rectangle_width, line, color);
    fill_rect(x, y, line, rectangle_height, color);
    fill_rect(saturate_i32(static_cast<int64_t>(x) + rectangle_width - line),
              y, line, rectangle_height, color);
}

void blend_corner_pixel(int32_t x, int32_t y, Color foreground,
                        uint8_t count) {
    if (count == 0U || !g_available || x < 0 || y < 0 ||
        x >= static_cast<int32_t>(g_framebuffer.width) ||
        y >= static_cast<int32_t>(g_framebuffer.height)) return;
    if (g_clip.enabled &&
        (x < g_clip.left || x >= g_clip.right ||
         y < g_clip.top || y >= g_clip.bottom)) return;
    if (!damage_contains(x, y)) return;
    auto* row = reinterpret_cast<uint32_t*>(
        draw_base() + static_cast<size_t>(y) * draw_pitch());
    const uint32_t fg = native_color(foreground);
    if (count >= 16U) { row[x] = fg; return; }
    const uint32_t bg = row[x];
    uint32_t out = bg & UINT32_C(0xff000000);
    for (uint32_t shift = 0U; shift < 24U; shift += 8U) {
        const uint32_t a = (fg >> shift) & 255U;
        const uint32_t b = (bg >> shift) & 255U;
        out |= ((a * count + b * (16U - count) + 8U) / 16U) << shift;
    }
    row[x] = out;
}

void fill_rounded_rect(int32_t x, int32_t y, int32_t width,
                       int32_t height, uint32_t radius, Color color) {
    if (!g_available || width <= 0 || height <= 0) return;
    uint32_t limit = radius > 16U ? 16U : radius;
    if (limit > static_cast<uint32_t>(width / 2)) limit = static_cast<uint32_t>(width / 2);
    if (limit > static_cast<uint32_t>(height / 2)) limit = static_cast<uint32_t>(height / 2);
    const int32_t r = static_cast<int32_t>(limit);
    if (r <= 1) { fill_rect(x, y, width, height, color); return; }
    // Cheap flat center and middle; only four radius-sized corners sampled.
    fill_rect(x, saturate_i32(static_cast<int64_t>(y) + r),
              width, height - 2 * r, color);
    fill_rect(saturate_i32(static_cast<int64_t>(x) + r), y,
              width - 2 * r, height, color);
    for (int32_t row = 0; row < r; ++row) {
        const int32_t top = saturate_i32(static_cast<int64_t>(y) + row);
        const int32_t bottom = saturate_i32(static_cast<int64_t>(y) + height - row - 1);
        for (int32_t col = 0; col < r; ++col) {
            const uint8_t a = raster::coverage4x4(r, col, row);
            const int32_t left = saturate_i32(static_cast<int64_t>(x) + col);
            const int32_t right = saturate_i32(static_cast<int64_t>(x) + width - col - 1);
            blend_corner_pixel(left, top, color, a);
            blend_corner_pixel(right, top, color, a);
            blend_corner_pixel(left, bottom, color, a);
            blend_corner_pixel(right, bottom, color, a);
        }
    }
}

void draw_char(int32_t x, int32_t y, char character, Color foreground,
               Color background, uint32_t scale, bool transparent) {
    scale = effective_scale(scale);
    if (!g_available || scale == 0U) return;
    const int32_t pixel_size =
        scale > static_cast<uint32_t>(INT32_MAX)
            ? INT32_MAX : static_cast<int32_t>(scale);
    const uint8_t* rows = glyph_rows(character);
    for (uint32_t row = 0U; row < 7U; ++row) {
        for (uint32_t column = 0U; column < 5U; ++column) {
            const bool set = rows && (rows[row] & (1u << (4u - column)));
            if (set || !transparent) {
                fill_rect(
                    saturate_i32(static_cast<int64_t>(x) +
                                 static_cast<int64_t>(column) * scale),
                    saturate_i32(static_cast<int64_t>(y) +
                                 static_cast<int64_t>(row) * scale),
                    pixel_size, pixel_size, set ? foreground : background);
            }
        }
        if (!transparent) {
            fill_rect(
                saturate_i32(static_cast<int64_t>(x) + INT64_C(5) * scale),
                saturate_i32(static_cast<int64_t>(y) +
                             static_cast<int64_t>(row) * scale),
                pixel_size, pixel_size, background);
        }
    }
    if (!transparent) {
        const uint64_t cell_width = UINT64_C(6) * scale;
        fill_rect(
            x,
            saturate_i32(static_cast<int64_t>(y) + INT64_C(7) * scale),
            cell_width > static_cast<uint64_t>(INT32_MAX)
                ? INT32_MAX : static_cast<int32_t>(cell_width),
            pixel_size, background);
    }
}

void draw_text(int32_t x, int32_t y, const char* text, Color foreground,
               Color background, uint32_t scale, bool transparent) {
    draw_text_styled(x, y, text, foreground, background, scale, transparent,
                     TextWeight::Normal);
}

namespace {
void draw_text_range(int32_t x, int32_t y, const char* text,
                     const char* end, Color foreground, Color background,
                     size_t size_index, bool transparent, TextWeight weight) {
    const size_t weight_offset = weight == TextWeight::Bold
        ? font_atlas::kSizeCount
        : 0U;
    const size_t font_style_index = weight_offset + size_index;
    const uint32_t ascent = font_atlas::kAscents[font_style_index];
    const uint32_t line_height = font_atlas::kLineHeights[font_style_index];
    int64_t cursor_x = x;
    int64_t cursor_y = y;
    while ((end == nullptr || text < end) && *text != '\0') {
        const ku_text::Scalar decoded = ku_text::next_z(text);
        if (decoded.bytes == 0U || (end != nullptr &&
                                    text + decoded.bytes > end)) break;
        if (decoded.value == '\n') {
            cursor_x = x;
            cursor_y = cursor_y > INT64_MAX - line_height
                ? INT64_MAX : cursor_y + line_height;
        } else if (decoded.value != '\r') {
            const size_t character_index = glyph_index(decoded.value);
            const font_atlas::Metrics& metrics =
                font_atlas::kMetrics[metrics_index(
                    size_index, character_index, weight)];
            if (!transparent && metrics.advance != 0U) {
                fill_rect(
                    saturate_i32(cursor_x),
                    saturate_i32(cursor_y),
                    metrics.advance,
                    static_cast<int32_t>(line_height),
                    background);
            }
            const int64_t baseline = cursor_y + ascent;
            for (uint32_t row = 0U; row < metrics.height; ++row) {
                for (uint32_t column = 0U; column < metrics.width; ++column) {
                    const uint32_t pixel = metrics.pixel_offset +
                        row * metrics.width + column;
                    const uint8_t packed = g_font_coverage4[pixel / 2U];
                    const uint32_t coverage = (pixel & 1U) == 0U
                        ? static_cast<uint32_t>(packed >> 4U)
                        : static_cast<uint32_t>(packed & 0x0fU);
                    if (coverage == 0U) continue;
                    const uint32_t coverage16 = (coverage * 16U + 7U) / 15U;
                    blend_corner_pixel(
                        saturate_i32(cursor_x + metrics.bearing_x + column),
                        saturate_i32(baseline + metrics.bearing_y + row),
                        foreground, static_cast<uint8_t>(coverage16));
                }
            }
            cursor_x = cursor_x > INT64_MAX - metrics.advance
                ? INT64_MAX : cursor_x + metrics.advance;
        }
        text += decoded.bytes;
    }
}
}

void draw_text_styled(int32_t x, int32_t y, const char* text,
                      Color foreground, Color background, uint32_t scale,
                      bool transparent, TextWeight weight) {
    if (text == nullptr) return;
    scale = effective_scale(scale);
    if (scale == 0U) return;
    draw_text_range(x, y, text, nullptr, foreground, background,
                    font_size_index(scale), transparent, weight);
}

uint32_t draw_text_ellipsized(
    int32_t x, int32_t y, const char* text, uint32_t max_width,
    Color foreground, Color background, uint32_t scale, bool transparent,
    TextWeight weight) {
    if (text == nullptr) return 0U;
    scale = effective_scale(scale);
    if (scale == 0U) return 0U;
    const size_t size_index = font_size_index(scale);
    const char* line_end = text;
    uint64_t full_width = 0U;
    while (*line_end != '\0') {
        const ku_text::Scalar decoded = ku_text::next_z(line_end);
        if (decoded.bytes == 0U || decoded.value == '\n') break;
        if (decoded.value != '\r') {
            const font_atlas::Metrics& metrics =
                font_atlas::kMetrics[metrics_index(
                    size_index, glyph_index(decoded.value), weight)];
            full_width = full_width > UINT32_MAX - metrics.advance
                ? UINT32_MAX
                : full_width + metrics.advance;
        }
        line_end += decoded.bytes;
    }
    if (full_width <= max_width) {
        draw_text_range(x, y, text, line_end, foreground, background,
                        size_index, transparent, weight);
        return static_cast<uint32_t>(full_width);
    }

    constexpr char kEllipsis[] = "...";
    const uint32_t ellipsis_width =
        measure_text_width(kEllipsis, scale, weight);
    if (max_width < ellipsis_width) return 0U;

    const uint32_t prefix_limit = max_width - ellipsis_width;
    uint32_t prefix_width = 0U;
    const char* prefix_end = text;
    while (prefix_end < line_end) {
        const ku_text::Scalar decoded = ku_text::next_z(prefix_end);
        if (decoded.bytes == 0U || prefix_end + decoded.bytes > line_end) break;
        if (decoded.value != '\r') {
            const font_atlas::Metrics& metrics =
                font_atlas::kMetrics[metrics_index(
                    size_index, glyph_index(decoded.value), weight)];
            if (metrics.advance > prefix_limit - prefix_width) break;
            prefix_width += metrics.advance;
        }
        prefix_end += decoded.bytes;
    }
    draw_text_range(x, y, text, prefix_end, foreground, background,
                    size_index, transparent, weight);
    draw_text_range(saturate_i32(static_cast<int64_t>(x) + prefix_width), y,
                    kEllipsis, kEllipsis + sizeof(kEllipsis) - 1U,
                    foreground, background, size_index, transparent, weight);
    return prefix_width + ellipsis_width;
}

uint32_t measure_text_width(const char* text, uint32_t scale, TextWeight weight) {
    if (text == nullptr) return 0U;
    scale = effective_scale(scale);
    if (scale == 0U) return 0U;
    const size_t size_index = font_size_index(scale);
    uint64_t cursor = 0U;
    uint64_t maximum = 0U;
    while (*text != '\0') {
        const ku_text::Scalar decoded = ku_text::next_z(text);
        if (decoded.bytes == 0U) break;
        if (decoded.value == '\n') {
            if (cursor > maximum) maximum = cursor;
            cursor = 0U;
        } else if (decoded.value != '\r') {
            const font_atlas::Metrics& metrics =
                font_atlas::kMetrics[metrics_index(
                    size_index, glyph_index(decoded.value), weight)];
            cursor = cursor > UINT32_MAX - metrics.advance
                ? UINT32_MAX
                : cursor + metrics.advance;
        }
        text += decoded.bytes;
    }
    if (cursor > maximum) maximum = cursor;
    return static_cast<uint32_t>(maximum);
}

void scroll_up(uint32_t pixels, Color fill) {
    if (!g_available || pixels == 0U) return;
    if (pixels >= g_framebuffer.height) {
        clear(fill);
        return;
    }
    reset_clip();
    const size_t pitch = draw_pitch();
    auto* base = draw_base();
    if (!g_damage_active) {
        const size_t bytes_to_move =
            static_cast<size_t>(g_framebuffer.height - pixels) * pitch;
        memmove(base, base + static_cast<size_t>(pixels) * pitch, bytes_to_move);
        fill_rect(0, static_cast<int32_t>(g_framebuffer.height - pixels),
                  static_cast<int32_t>(g_framebuffer.width),
                  static_cast<int32_t>(pixels), fill);
        return;
    }

    const uint32_t native = native_color(fill);
    // Process destination rows top-to-bottom.  Every scroll source row is
    // strictly below its destination, so source pixels are read before they
    // can be modified even when damage regions overlap.
    for (uint32_t y = 0U; y < g_framebuffer.height; ++y) {
        for (size_t index = 0U; index < g_damage_count; ++index) {
            const DamageRect& region = g_damage_regions[index];
            if (y < static_cast<uint32_t>(region.y) ||
                y >= static_cast<uint32_t>(region.y + region.height)) continue;
            auto* destination = reinterpret_cast<uint32_t*>(
                base + static_cast<size_t>(y) * pitch) + region.x;
            const size_t span = static_cast<size_t>(region.width);
            if (y + pixels < g_framebuffer.height) {
                const auto* source = reinterpret_cast<const uint32_t*>(
                    base + static_cast<size_t>(y + pixels) * pitch) + region.x;
                memmove(destination, source, span * sizeof(uint32_t));
            } else {
                for (size_t x = 0U; x < span; ++x) destination[x] = native;
            }
        }
    }
}

} // namespace graphics

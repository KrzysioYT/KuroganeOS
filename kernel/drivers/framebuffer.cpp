#include "framebuffer.hpp"

#include "../core/memory.hpp"

namespace graphics {

namespace {
KuroganeFramebuffer g_framebuffer{};
bool g_available = false;

// Kurogane 5 software compositor surface. 1600x1200 covers the current QEMU
// development GOP modes while avoiding dependence on the small kernel heap.
constexpr uint32_t kBackbufferWidth = 1600U;
constexpr uint32_t kBackbufferHeight = 1200U;
constexpr size_t kBackbufferPixels =
    static_cast<size_t>(kBackbufferWidth) * kBackbufferHeight;
alignas(64) static uint32_t g_backbuffer[kBackbufferPixels];
// Last frame that was actually presented to GOP. Comparing backbuffer against
// this RAM shadow avoids reading emulated GOP/VRAM for every pixel on every
// frame, which is especially expensive under QEMU TCG.
alignas(64) static uint32_t g_front_shadow[kBackbufferPixels];
bool g_front_shadow_valid = false;
bool g_frame_active = false;

struct ClipState {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
    bool enabled;
};

ClipState g_clip{};
uint32_t g_text_scale_limit = UINT32_MAX;

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
    {'a', {0x00, 0x0E, 0x01, 0x0F, 0x11, 0x13, 0x0D}},
    {'b', {0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x1E}},
    {'c', {0x00, 0x0E, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'d', {0x01, 0x01, 0x0F, 0x11, 0x11, 0x11, 0x0F}},
    {'e', {0x00, 0x0E, 0x11, 0x1F, 0x10, 0x11, 0x0E}},
    {'f', {0x06, 0x08, 0x1E, 0x08, 0x08, 0x08, 0x08}},
    {'g', {0x00, 0x0F, 0x11, 0x0F, 0x01, 0x11, 0x0E}},
    {'h', {0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x11}},
    {'i', {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E}},
    {'j', {0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C}},
    {'k', {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12}},
    {'l', {0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'m', {0x00, 0x1A, 0x15, 0x15, 0x15, 0x15, 0x15}},
    {'n', {0x00, 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11}},
    {'o', {0x00, 0x0E, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'p', {0x00, 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10}},
    {'q', {0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x01}},
    {'r', {0x00, 0x16, 0x19, 0x10, 0x10, 0x10, 0x10}},
    {'s', {0x00, 0x0F, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'t', {0x08, 0x08, 0x1E, 0x08, 0x08, 0x09, 0x06}},
    {'u', {0x00, 0x11, 0x11, 0x11, 0x11, 0x13, 0x0D}},
    {'v', {0x00, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'w', {0x00, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {'x', {0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00}},
    {'y', {0x00, 0x11, 0x11, 0x0F, 0x01, 0x11, 0x0E}},
    {'z', {0x00, 0x1F, 0x02, 0x04, 0x08, 0x10, 0x1F}},
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

constexpr Color kForgedObsidian = UINT32_C(0x090E0E);
constexpr Color kForgedLegacyObsidian = UINT32_C(0x090A0C);
constexpr Color kForgedSteel = UINT32_C(0x171C22);
constexpr Color kForgedSteelEdge = UINT32_C(0x343B43);
constexpr Color kForgedAsh = UINT32_C(0xA8AFB8);
constexpr Color kForgedText = UINT32_C(0xE9EDF1);
constexpr Color kForgedCrimson = UINT32_C(0xE62932);
constexpr Color kForgedHotEdge = UINT32_C(0xFF4A45);

uint32_t native_color(Color color) {
    if (g_framebuffer.pixel_format == KUROGANE_PIXEL_RGBX8) {
        return ((color & 0x0000FFu) << 16) |
               (color & 0x00FF00u) |
               ((color & 0xFF0000u) >> 16);
    }
    return color;
}

Color logical_color(uint32_t color) {
    if (g_framebuffer.pixel_format == KUROGANE_PIXEL_RGBX8) {
        return ((color & UINT32_C(0x0000FF)) << 16U) |
               (color & UINT32_C(0x00FF00)) |
               ((color & UINT32_C(0xFF0000)) >> 16U);
    }
    return color & UINT32_C(0x00FFFFFF);
}

const uint8_t* glyph_rows(char character) {
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

bool text_starts_with(const char* text, const char* prefix) {
    if (text == nullptr || prefix == nullptr) return false;
    while (*prefix != '\0') {
        if (*text++ != *prefix++) return false;
    }
    return true;
}

bool forged_header(const char* text) {
    return text_starts_with(text, "VAULT") ||
        text_starts_with(text, "ANVIL") ||
        text_starts_with(text, "FORGE") ||
        text_starts_with(text, "KUROSH") ||
        text_starts_with(text, "PULSE") ||
        text_starts_with(text, "PERFORMANCE") ||
        text_starts_with(text, "SYSTEM MONITOR") ||
        text_starts_with(text, "KUROGANEOS") ||
        text_starts_with(text, "ABOUT") ||
        text_starts_with(text, "BLADE");
}

bool forged_background(Color background) {
    const Color value = background & UINT32_C(0xFFFFFF);
    return value == kForgedObsidian || value == kForgedLegacyObsidian;
}

int32_t forged_available_width(int32_t x) {
    int32_t right = g_clip.enabled
        ? g_clip.right
        : static_cast<int32_t>(g_framebuffer.width);
    if (right <= x + 16) return 0;
    int32_t width = right - x - 8;
    if (width > 680) width = 680;
    return width;
}

void fill_chamfered(
    int32_t x, int32_t y, int32_t width, int32_t height,
    int32_t cut, Color color) {
    if (width <= 0 || height <= 0) return;
    if (cut < 0) cut = 0;
    if (cut * 2 > height) cut = height / 2;
    for (int32_t row = 0; row < height; ++row) {
        int32_t inset = 0;
        if (row < cut) inset = cut - row;
        else if (row >= height - cut) inset = row - (height - cut - 1);
        if (inset * 2 >= width) continue;
        fill_rect(x + inset, y + row, width - inset * 2, 1, color);
    }
}

void draw_text_plain(
    int32_t x, int32_t y, const char* text, Color foreground,
    Color background, uint32_t scale, bool transparent) {
    if (!text || scale == 0U) return;
    int64_t cursor_x = x;
    int64_t cursor_y = y;
    const int64_t line_advance = static_cast<uint64_t>(scale) * 8u;
    const int64_t column_advance = static_cast<uint64_t>(scale) * 6u;
    while (*text) {
        if (*text == '\n') {
            cursor_x = x;
            cursor_y = cursor_y > INT64_MAX - line_advance
                ? INT64_MAX : cursor_y + line_advance;
        } else {
            draw_char(saturate_i32(cursor_x), saturate_i32(cursor_y),
                      *text, foreground, background, scale, transparent);
            cursor_x = cursor_x > INT64_MAX - column_advance
                ? INT64_MAX : cursor_x + column_advance;
        }
        ++text;
    }
}

bool draw_forged_application_line(
    int32_t x, int32_t y, const char* text, Color foreground,
    Color background, uint32_t requested_scale, bool transparent) {
    if (!text || !transparent || requested_scale < 2U ||
        !forged_background(background) || !g_clip.enabled) {
        return false;
    }

    const int32_t width = forged_available_width(x);
    if (width < 96) return false;

    const bool selected = text[0] == '>' && text[1] == ' ';
    const bool nested = text[0] == ' ' && text[1] == ' ';
    const bool separator = text_starts_with(text, "-----");
    const bool header = forged_header(text);

    if (separator) {
        fill_rect(x + 2, y + 7, width - 4, 1, kForgedSteelEdge);
        fill_rect(x + 2, y + 7, width > 86 ? 84 : width - 4, 1, kForgedCrimson);
        return true;
    }

    if (header) {
        const int32_t box_y = y - 4;
        fill_chamfered(x, box_y, width, 20, 5, kForgedSteelEdge);
        fill_chamfered(x + 1, box_y + 1, width - 2, 18, 4, kForgedSteel);
        fill_rect(x + 8, box_y + 1, width > 112 ? 104 : width - 16, 1,
                  kForgedHotEdge);
        fill_rect(x + 5, box_y + 5, 2, 10, kForgedCrimson);
        draw_text_plain(x + 14, y + 2, text, kForgedText,
                        kForgedSteel, 1U, true);
        return true;
    }

    if (selected || nested) {
        const char* label = text + 2;
        const int32_t card_x = x + (nested ? 7 : 2);
        const int32_t card_width = width - (nested ? 12 : 4);
        const int32_t box_y = y - 2;
        const Color edge = selected ? kForgedHotEdge : kForgedSteelEdge;
        const Color fill = selected ? UINT32_C(0x20262D) : kForgedSteel;
        fill_chamfered(card_x, box_y, card_width, 19, 4, edge);
        fill_chamfered(card_x + 1, box_y + 1, card_width - 2, 17, 3, fill);
        if (selected) {
            fill_rect(card_x + 4, box_y + 4, 3, 11, kForgedHotEdge);
            fill_rect(card_x + 7, box_y + 6, 1, 7, kForgedCrimson);
        } else {
            fill_rect(card_x + 4, box_y + 6, 1, 7, kForgedCrimson);
        }
        draw_text_plain(card_x + 13, y + 3, label,
                        selected ? kForgedText : kForgedAsh,
                        fill, 1U, true);
        return true;
    }

    // Root labels become compact steel typography instead of double-sized
    // 5x7 text. This is deliberately scoped to Forged Steel application
    // surfaces so browser/page rendering keeps its own typography choices.
    draw_text_plain(x + 3, y + 3, text,
                    (foreground & UINT32_C(0xFFFFFF)) == kForgedCrimson
                        ? kForgedHotEdge : kForgedAsh,
                    background, 1U, true);
    return true;
}
} // namespace

bool init(const KuroganeFramebuffer& framebuffer) {
    g_available = false;
    g_frame_active = false;
    g_front_shadow_valid = false;
    g_clip = {};
    g_text_scale_limit = UINT32_MAX;
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
    reset_clip();
    reset_text_scale_limit();
    return true;
}

void end_frame() {
    if (!g_available || !g_frame_active) return;

    // Never compare against GOP/VRAM directly. Emulated framebuffer reads are
    // disproportionately expensive under QEMU/TCG. The first compositor frame
    // establishes a RAM shadow; later frames compare RAM-to-RAM and only write
    // changed horizontal spans to GOP.
    auto* framebuffer_bytes = reinterpret_cast<uint8_t*>(g_framebuffer.base);
    const uint32_t frame_width = g_framebuffer.width;
    const size_t row_bytes =
        static_cast<size_t>(frame_width) * sizeof(uint32_t);
    for (uint32_t y = 0U; y < g_framebuffer.height; ++y) {
        auto* destination_row = reinterpret_cast<uint32_t*>(
            framebuffer_bytes + static_cast<size_t>(y) * g_framebuffer.pitch);
        const auto* source_row = g_backbuffer +
            static_cast<size_t>(y) * static_cast<size_t>(frame_width);
        auto* shadow_row = g_front_shadow +
            static_cast<size_t>(y) * static_cast<size_t>(frame_width);

        if (!g_front_shadow_valid) {
            memcpy(destination_row, source_row, row_bytes);
            memcpy(shadow_row, source_row, row_bytes);
            continue;
        }

        uint32_t first_changed = 0U;
        while (first_changed < frame_width &&
               shadow_row[first_changed] == source_row[first_changed]) {
            ++first_changed;
        }
        if (first_changed == frame_width) continue;

        uint32_t last_changed = frame_width;
        while (last_changed > first_changed &&
               shadow_row[last_changed - 1U] == source_row[last_changed - 1U]) {
            --last_changed;
        }

        const size_t changed_pixels = static_cast<size_t>(
            last_changed - first_changed);
        const size_t changed_bytes = changed_pixels * sizeof(uint32_t);
        memcpy(
            destination_row + first_changed,
            source_row + first_changed,
            changed_bytes);
        memcpy(
            shadow_row + first_changed,
            source_row + first_changed,
            changed_bytes);
    }
    g_front_shadow_valid = true;
    g_frame_active = false;
    reset_clip();
    reset_text_scale_limit();
}

bool frame_active() { return g_frame_active; }

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

void put_pixel(int32_t x, int32_t y, Color color) {
    if (!g_available || x < 0 || y < 0 ||
        x >= static_cast<int32_t>(g_framebuffer.width) ||
        y >= static_cast<int32_t>(g_framebuffer.height)) return;
    if (g_clip.enabled &&
        (x < g_clip.left || x >= g_clip.right ||
         y < g_clip.top || y >= g_clip.bottom)) return;
    auto* row = reinterpret_cast<uint32_t*>(
        draw_base() + static_cast<size_t>(y) * draw_pitch());
    row[x] = native_color(color);
}

Color get_pixel(int32_t x, int32_t y) {
    if (!g_available || x < 0 || y < 0 ||
        x >= static_cast<int32_t>(g_framebuffer.width) ||
        y >= static_cast<int32_t>(g_framebuffer.height)) return 0U;
    const auto* row = reinterpret_cast<const uint32_t*>(
        draw_base() + static_cast<size_t>(y) * draw_pitch());
    return logical_color(row[x]);
}

void blend_pixel(int32_t x, int32_t y, Color color, uint8_t alpha) {
    if (alpha == 0U) return;
    if (alpha == UINT8_MAX) {
        put_pixel(x, y, color);
        return;
    }
    const Color background = get_pixel(x, y);
    const uint32_t inverse = UINT32_C(255) - alpha;
    const uint32_t red = (((color >> 16U) & UINT32_C(0xFF)) * alpha +
                          ((background >> 16U) & UINT32_C(0xFF)) * inverse + 127U) / 255U;
    const uint32_t green = (((color >> 8U) & UINT32_C(0xFF)) * alpha +
                            ((background >> 8U) & UINT32_C(0xFF)) * inverse + 127U) / 255U;
    const uint32_t blue = ((color & UINT32_C(0xFF)) * alpha +
                           (background & UINT32_C(0xFF)) * inverse + 127U) / 255U;
    put_pixel(x, y, (red << 16U) | (green << 8U) | blue);
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
    for (int32_t py = static_cast<int32_t>(top);
         py < static_cast<int32_t>(bottom); ++py) {
        auto* row = reinterpret_cast<uint32_t*>(
            draw_base() + static_cast<size_t>(py) * draw_pitch());
        for (int32_t px = static_cast<int32_t>(left);
             px < static_cast<int32_t>(right); ++px) {
            row[px] = native;
        }
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
    if (!text) return;
    const uint32_t requested_scale = scale;
    scale = effective_scale(scale);
    if (scale == 0U) return;
    if (draw_forged_application_line(
            x, y, text, foreground, background, requested_scale, transparent)) {
        return;
    }
    draw_text_plain(x, y, text, foreground, background, scale, transparent);
}

void scroll_up(uint32_t pixels, Color fill) {
    if (!g_available || pixels == 0U) return;
    if (pixels >= g_framebuffer.height) {
        clear(fill);
        return;
    }
    reset_clip();
    const size_t pitch = draw_pitch();
    const size_t bytes_to_move =
        static_cast<size_t>(g_framebuffer.height - pixels) * pitch;
    auto* base = draw_base();
    memmove(base, base + static_cast<size_t>(pixels) * pitch, bytes_to_move);
    fill_rect(0, static_cast<int32_t>(g_framebuffer.height - pixels),
              static_cast<int32_t>(g_framebuffer.width),
              static_cast<int32_t>(pixels), fill);
}

} // namespace graphics

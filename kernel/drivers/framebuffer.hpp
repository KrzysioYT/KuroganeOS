#pragma once

#include "../../common/boot_protocol.h"

#include <stddef.h>
#include <stdint.h>

namespace graphics {

using Color = uint32_t;

constexpr Color rgb(uint8_t red, uint8_t green, uint8_t blue) {
    return (static_cast<Color>(red) << 16) |
           (static_cast<Color>(green) << 8) |
           static_cast<Color>(blue);
}

bool init(const KuroganeFramebuffer& framebuffer);
bool available();
const KuroganeFramebuffer& info();
uint32_t width();
uint32_t height();

// Software frame support used by Flux WindowManager. begin_frame() switches
// drawing to an off-screen buffer when the active GOP mode fits the built-in
// compositor surface. end_frame() copies one complete finished frame to GOP.
// Oversized modes safely keep the existing direct-render path.
bool begin_frame();
void end_frame();
bool frame_active();

// All primitive drawing honors the current clip. WindowManager uses this to
// guarantee that application content cannot overwrite window chrome or a
// neighboring surface even when a legacy ku_ui_frame line is too long.
void set_clip(int32_t x, int32_t y, int32_t width, int32_t height);
void reset_clip();

// Body text may request scale=2 through legacy paths. WindowManager can apply
// a temporary upper bound for compact content areas without affecting chrome.
void set_text_scale_limit(uint32_t maximum_scale);
void reset_text_scale_limit();

void put_pixel(int32_t x, int32_t y, Color color);
void clear(Color color);
void fill_rect(int32_t x, int32_t y, int32_t width, int32_t height,
               Color color);
void draw_rect(int32_t x, int32_t y, int32_t width, int32_t height,
               Color color, uint32_t thickness = 1);
void draw_char(int32_t x, int32_t y, char character, Color foreground,
               Color background, uint32_t scale = 1,
               bool transparent = false);
void draw_text(int32_t x, int32_t y, const char* text, Color foreground,
               Color background, uint32_t scale = 1,
               bool transparent = false);
void scroll_up(uint32_t pixels, Color fill);

} // namespace graphics

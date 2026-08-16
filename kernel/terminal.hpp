#pragma once

#include "../common/boot_protocol.h"

#include <stddef.h>
#include <stdint.h>

namespace terminal {

bool configure(const KuroganeFramebuffer& framebuffer);
void init();
bool ready();

// The boot/emergency terminal and Flux Desktop share the GOP framebuffer.
// Serial output always remains enabled, but once the desktop owns the display
// the terminal must stop drawing glyphs or scrolling framebuffer memory.
void set_framebuffer_output(bool enabled);
bool framebuffer_output_enabled();

void put(char character);
void write(const char* text);
void println(const char* text = nullptr);
void clear();
void backspace();
void set_colors(uint32_t foreground, uint32_t background);
void reset_colors();
void write_u64(uint64_t value);
void write_hex(uint64_t value);
size_t columns();
size_t rows();
size_t cursor_column();
size_t cursor_row();

} // namespace terminal

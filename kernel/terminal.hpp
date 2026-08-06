#pragma once

#include "../common/boot_protocol.h"

#include <stddef.h>
#include <stdint.h>

namespace terminal {

bool configure(const KuroganeFramebuffer& framebuffer);
void init();
bool ready();
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

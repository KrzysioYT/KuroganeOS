#pragma once

#include <stdint.h>

namespace serial {

bool init(uint16_t port = 0x3F8);
bool ready();
void put(char character);
void write(const char* text);
void write_hex(uint64_t value);

} // namespace serial

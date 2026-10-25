#include "../kernel/core/log.hpp"

namespace log {

void set_minimum_level(Level) {}
Level minimum_level() { return Level::Info; }
void write(Level, const char*, const char*) {}
void write_u64(Level, const char*, const char*, uint64_t) {}
void write_hex(Level, const char*, const char*, uint64_t) {}

} // namespace log

#pragma once

#include <stdint.h>

namespace log {

enum class Level : uint8_t {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

void set_minimum_level(Level level);
Level minimum_level();

// Logging is allocation-free and safe to use before the kernel heap exists.
void write(Level level, const char* module, const char* message);
void write_u64(
    Level level,
    const char* module,
    const char* message,
    uint64_t value);
void write_hex(
    Level level,
    const char* module,
    const char* message,
    uint64_t value);

} // namespace log

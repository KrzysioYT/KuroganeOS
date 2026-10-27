#pragma once

#include <stdint.h>

// Hosted GCC recognizes the C math builtin log even without <cmath>. The
// kernel's logger deliberately uses this namespace. Limit the diagnostic
// exception to its declaration; keep all warnings in callers enabled.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"
#endif
namespace log {
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

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

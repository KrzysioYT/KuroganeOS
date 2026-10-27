#pragma once

#include <stdint.h>

// The kernel's historical "log" namespace is also the hosted C runtime's
// builtin math function name. Host-only GCC builds with -Werror diagnose
// that spelling even when no libm declarations are used. Keep suppression
// strictly scoped to this existing declaration; do not disable unrelated
// warnings across driver/security tests. Migrate to a distinct namespace in
// a future coordinated kernel ABI refactor.

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
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

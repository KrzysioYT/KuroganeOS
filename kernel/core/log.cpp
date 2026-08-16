#include "log.hpp"

#include "../drivers/serial.hpp"
#include "../terminal.hpp"

namespace log {
namespace {

Level g_minimum_level = Level::Info;

const char* level_name(Level level) {
    switch (level) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info: return "INFO";
        case Level::Warn: return "WARN";
        case Level::Error: return "ERROR";
        case Level::Fatal: return "FATAL";
    }
    return "UNKNOWN";
}

bool enabled(Level level) {
    return static_cast<uint8_t>(level) >=
        static_cast<uint8_t>(g_minimum_level);
}

void emit_text(const char* text) {
    if (!text) {
        text = "(null)";
    }
    if (terminal::ready()) {
        terminal::write(text);
    } else {
        serial::write(text);
    }
}

void emit_character(char character) {
    if (terminal::ready()) {
        terminal::put(character);
    } else {
        serial::put(character);
    }
}

void emit_u64(uint64_t value) {
    char digits[21];
    size_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count != 0) {
        emit_character(digits[--count]);
    }
}

void emit_hex(uint64_t value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    emit_text("0x");
    bool significant = false;
    for (int shift = 60; shift >= 0; shift -= 4) {
        const uint8_t digit = static_cast<uint8_t>((value >> shift) & 0x0F);
        if (digit != 0 || significant || shift == 0) {
            significant = true;
            emit_character(digits[digit]);
        }
    }
}

void prefix(Level level, const char* module) {
    emit_text("[");
    emit_text(level_name(level));
    emit_text("][");
    emit_text(module ? module : "KERNEL");
    // The callback dispatcher is not a process/thread scheduler. Do not label
    // its slot number as a TID; a real PID/TID context will be added together
    // with the process model.
    emit_text("][CPU0][KERNEL] ");
}

void newline() {
    emit_character('\n');
}

} // namespace

void set_minimum_level(Level level) {
    g_minimum_level = level;
}

Level minimum_level() {
    return g_minimum_level;
}

void write(Level level, const char* module, const char* message) {
    if (!enabled(level)) {
        return;
    }
    prefix(level, module);
    emit_text(message);
    newline();
}

void write_u64(
    Level level,
    const char* module,
    const char* message,
    uint64_t value) {
    if (!enabled(level)) {
        return;
    }
    prefix(level, module);
    emit_text(message);
    emit_u64(value);
    newline();
}

void write_hex(
    Level level,
    const char* module,
    const char* message,
    uint64_t value) {
    if (!enabled(level)) {
        return;
    }
    prefix(level, module);
    emit_text(message);
    emit_hex(value);
    newline();
}

} // namespace log

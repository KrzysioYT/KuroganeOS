#include "serial.hpp"

#include "../arch/x86_64/io.hpp"

#include <stddef.h>

namespace serial {

namespace {
uint16_t g_port = 0x3F8;
bool g_ready = false;

bool transmit_empty() {
    return (arch::in8(static_cast<uint16_t>(g_port + 5)) & 0x20u) != 0;
}
} // namespace

bool init(uint16_t port) {
    g_port = port;
    arch::out8(static_cast<uint16_t>(g_port + 1), 0x00);
    arch::out8(static_cast<uint16_t>(g_port + 3), 0x80);
    arch::out8(static_cast<uint16_t>(g_port + 0), 0x03);
    arch::out8(static_cast<uint16_t>(g_port + 1), 0x00);
    arch::out8(static_cast<uint16_t>(g_port + 3), 0x03);
    arch::out8(static_cast<uint16_t>(g_port + 2), 0xC7);
    arch::out8(static_cast<uint16_t>(g_port + 4), 0x0B);
    arch::out8(static_cast<uint16_t>(g_port + 4), 0x1E);
    arch::out8(g_port, 0xAE);
    g_ready = arch::in8(g_port) == 0xAE;
    arch::out8(static_cast<uint16_t>(g_port + 4), 0x0F);
    return g_ready;
}

bool ready() {
    return g_ready;
}

void put(char character) {
    if (!g_ready) {
        return;
    }
    for (size_t spin = 0; spin < 100000 && !transmit_empty(); ++spin) {
        arch::pause();
    }
    if (transmit_empty()) {
        arch::out8(g_port, static_cast<uint8_t>(character));
    }
}

void write(const char* text) {
    if (!text) {
        return;
    }
    while (*text) {
        if (*text == '\n') {
            put('\r');
        }
        put(*text++);
    }
}

void write_hex(uint64_t value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    write("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        put(digits[(value >> shift) & 0x0F]);
    }
}

} // namespace serial

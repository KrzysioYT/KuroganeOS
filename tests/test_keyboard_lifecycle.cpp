#include <cassert>
#include <cstdint>
#include <deque>
namespace keyboard_test_io {
bool present = false;
std::deque<uint8_t> responses;
void write(uint16_t port, uint8_t value) {
    if (!present) return;
    if (port == 0x64 && value == 0x20) responses.push_back(0);
    if (port == 0x60 && value == 0xf4) responses.push_back(0xfa);
}
uint8_t read(uint16_t port) {
    if (port == 0x64) return responses.empty() ? 0 : 1;
    if (responses.empty()) return 0xff;
    const auto value = responses.front(); responses.pop_front(); return value;
}
}
#define KUROGANE_KEYBOARD_HOST_TEST
#include "../kernel/drivers/keyboard.cpp"
namespace fixture { bool registered = false, masked = true, allow_irq = true; }
namespace drivers::pic {
bool mask(uint8_t) { fixture::masked = true; return true; }
bool unmask(uint8_t) {
    if (!fixture::allow_irq) return false;
    fixture::masked = false; return true;
}
}
namespace arch::x86_64::interrupts {
bool register_irq_handler(uint8_t, IrqHandler) { fixture::registered = true; return true; }
void unregister_irq_handler(uint8_t) { fixture::registered = false; }
bool enabled() { return false; }
void enable() {}
void disable() {}
}
namespace drivers::mouse { void process_byte(uint8_t) {} }
int main() {
    using namespace drivers::keyboard;
    assert(!initialize());
    assert(!initialized() && !controller_configured());
    assert(fixture::masked && !fixture::registered);
    keyboard_test_io::present = true;
    assert(initialize() && initialized() && controller_configured());
    assert(!fixture::masked && fixture::registered);
    shutdown();
    assert(!initialized() && fixture::masked && !fixture::registered);
    fixture::allow_irq = false;
    assert(!initialize() && !initialized());
    assert(fixture::masked && !fixture::registered);
    keyboard_test_io::present = false;
    assert(!initialize() && !initialized() && !controller_configured());
}

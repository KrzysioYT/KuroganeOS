#include "assert.hpp"

#include "../arch/x86_64/io.hpp"
#include "format.hpp"
#include "logging.hpp"

namespace libk {

[[noreturn]] void assertion_failed(
    const char* expression,
    const char* file,
    unsigned int line) {
    char message[256];
    static_cast<void>(k_snprintf(
        message,
        sizeof(message),
        "assertion failed: %s (%s:%u)",
        expression,
        file,
        line));
    logging::write(logging::Level::Fatal, "ASSERT", message);
    arch::disable_interrupts();
    for (;;) {
        arch::halt();
    }
}

} // namespace libk

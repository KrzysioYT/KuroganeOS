#include "../kernel/core/log.hpp"
#include <cstdint>

// Compile-only portability contract. Hosted GCC and Apple Clang must not
// reject the kernel's freestanding logger under their strict warning flags.
// No fake kernel output is needed for this header regression.
static_assert(static_cast<uint8_t>(log::Level::Info) == 2U,
              "kernel log level ABI changed");
static_assert(static_cast<uint8_t>(log::Level::Fatal) == 5U,
              "kernel log level ABI changed");
int main() { return 0; }

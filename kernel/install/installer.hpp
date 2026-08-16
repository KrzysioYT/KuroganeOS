#pragma once

#include <stddef.h>

namespace install::installer {

// Runs the destructive path only after an explicit device index and the exact
// confirmation word INSTALL have been entered at the guest console.
[[noreturn]] void run_interactive(
    const void* package_bytes,
    size_t package_size);

} // namespace install::installer

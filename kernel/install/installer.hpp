#pragma once

#include <stddef.h>

namespace install::installer {

// 3.3 dev setup entry. Installation media first presents a non-destructive
// Try/Install choice. Choosing Try mounts install.pkg as a read-only live root
// and returns to the normal boot path. Choosing Install runs the destructive
// disk deployment only after language/account setup, disk selection and an
// explicit confirmation.
void run_interactive(
    const void* package_bytes,
    size_t package_size);

} // namespace install::installer

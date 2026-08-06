#pragma once

#include <kurogane/abi.h>

namespace abi {

// Describes services reachable through the public application ABI. Kernel
// internal facilities are not advertised until a user-mode transport exists.
const ku_abi_descriptor& descriptor();
bool application_transport_available();

} // namespace abi

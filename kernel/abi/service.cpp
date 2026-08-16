#include "service.hpp"

#include "../user/runtime.hpp"

namespace abi {
namespace {

constexpr ku_abi_descriptor g_descriptor{
    sizeof(ku_abi_descriptor),
    KU_ABI_VERSION_CURRENT,
    KU_ARCHITECTURE_X86_64,
    4096,
    0, // WRITE/EXIT have no matching capability bit; process/files stay off.
    {0, 0, 0}
};

static_assert(sizeof(g_descriptor) == 48, "kernel ABI layout mismatch");
static_assert(
    (KU_ABI_VERSION_CURRENT >> 16) == KU_ABI_VERSION_MAJOR,
    "kernel ABI version encoding mismatch");

} // namespace

const ku_abi_descriptor& descriptor() {
    return g_descriptor;
}

bool application_transport_available() {
    return user::runtime::initialized();
}

} // namespace abi

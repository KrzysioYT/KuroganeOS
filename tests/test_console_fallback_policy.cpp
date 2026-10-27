#include "../kernel/user/console_fallback_policy.hpp"
#include <cassert>
#include <cstdint>
int main() {
    using user::console_fallback_policy::allowed;
    static_assert(allowed(1U, 0U, 0U, 0U), "PID1 has recovery access");
    static_assert(!allowed(0U, 0U, 0U, 0U), "kernel pseudo PID is not an app");
    static_assert(!allowed(2U, 0U, 0U, 0U), "Ring-3 user apps cannot stop Flux");
    static_assert(!allowed(1U, 1U, 0U, 0U), "reserved argument 1");
    static_assert(!allowed(1U, 0U, 1U, 0U), "reserved argument 2");
    static_assert(!allowed(1U, 0U, 0U, 1U), "reserved argument 3");
    assert(allowed(UINT64_C(1), 0U, 0U, 0U));
    assert(!allowed(UINT64_MAX, 0U, 0U, 0U));
    return 0;
}

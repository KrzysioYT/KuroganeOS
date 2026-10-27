#include "../kernel/ui/session_owner_policy.hpp"
#include <cassert>
#include <cstdint>
int main() {
    using namespace windowing::session_owner_policy;
    static_assert(login_allowed(2U, 1U), "PID1 child owns login");
    static_assert(!login_allowed(3U, 2U), "app must not replace login");
    static_assert(!login_allowed(1U, 1U), "init itself is not login");
    static_assert(home_allowed(3U, 2U, 2U), "login child owns Home");
    static_assert(!home_allowed(4U, 3U, 2U), "grandchild cannot seize Home");
    static_assert(!home_allowed(2U, 2U, 2U), "login cannot impersonate Home");
    static_assert(!home_allowed(3U, 1U, 2U), "PID1 child is not Home");
    static_assert(!home_allowed(3U, 2U, 0U), "absent Login");
    assert(home_allowed(1000U, 42U, 42U));
    return 0;
}

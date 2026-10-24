#include "../kernel/user/child_termination_policy.hpp"
#include <cassert>
int main() {
    using user::child_termination_policy::allowed;
    static_assert(allowed(7U, 10U, 7U), "parent may stop child");
    static_assert(!allowed(7U, 7U, 7U), "self denied");
    static_assert(!allowed(7U, 1U, 7U), "init denied");
    static_assert(!allowed(7U, 9U, 8U), "siblings denied");
    static_assert(!allowed(7U, 9U, 0U), "orphans denied");
    for (uint64_t caller = 0U; caller < 50U; ++caller)
        for (uint64_t target = 0U; target < 50U; ++target)
            for (uint64_t parent = 0U; parent < 50U; ++parent)
                assert(allowed(caller,target,parent) ==
                    (caller != 0U && target > 1U &&
                     caller != target && parent == caller));
    return 0;
}

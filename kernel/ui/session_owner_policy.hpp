#pragma once
#include <stdint.h>

namespace windowing::session_owner_policy {
// The title of a UI window is not an authentication token. Only PID1's
// direct child may own Login; only that Login's direct child may own Home.
constexpr bool login_allowed(uint64_t pid, uint64_t parent_pid) {
    return pid > UINT64_C(1) && parent_pid == UINT64_C(1);
}
constexpr bool home_allowed(uint64_t pid, uint64_t parent_pid,
                            uint64_t live_login_pid) {
    return pid > UINT64_C(1) && live_login_pid > UINT64_C(1) &&
           pid != live_login_pid && parent_pid == live_login_pid;
}
}

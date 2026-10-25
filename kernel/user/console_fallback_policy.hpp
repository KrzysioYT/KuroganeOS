#pragma once
#include <stdint.h>

namespace user::console_fallback_policy {
// The emergency visible terminal mode is a privileged recovery operation,
// never an application shortcut or ordinary user-accessible UI action.
constexpr bool allowed(uint64_t caller_pid, uint64_t argument1,
                       uint64_t argument2, uint64_t argument3) {
    return caller_pid == UINT64_C(1) &&
        argument1 == 0U && argument2 == 0U && argument3 == 0U;
}
} // namespace user::console_fallback_policy

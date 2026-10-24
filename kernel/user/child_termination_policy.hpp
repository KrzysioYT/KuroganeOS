#pragma once
#include <stdint.h>
namespace user::child_termination_policy {
constexpr bool allowed(uint64_t caller, uint64_t target, uint64_t parent) {
    return caller != 0U && target > 1U && caller != target && parent == caller;
}
}

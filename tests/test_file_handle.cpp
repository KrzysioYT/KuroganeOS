#include "../kernel/user/file_handle.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>

namespace {

struct TestSlot {
    uint32_t generation;
    bool active;
    uint32_t value;
};

uint64_t handle(size_t index, uint32_t generation) {
    return (static_cast<uint64_t>(generation) << 32U) |
        static_cast<uint64_t>(index + user::file_handle::FIRST_DESCRIPTOR);
}

} // namespace

int main() {
    using user::file_handle::Resolution;

    TestSlot slots[2]{};
    slots[0] = {7U, true, 0xA0U};

    auto resolved = user::file_handle::resolve(slots, 2U, handle(0U, 7U));
    assert(resolved.resolution == Resolution::Ok);
    assert(resolved.slot == &slots[0]);
    assert(resolved.slot->value == 0xA0U);

    // A successful close makes the same well-formed descriptor stale.
    slots[0].active = false;
    resolved = user::file_handle::resolve(slots, 2U, handle(0U, 7U));
    assert(resolved.resolution == Resolution::Stale);
    assert(resolved.slot == nullptr);

    // Reusing the physical slot increments its generation. The old handle
    // remains stale and can never alias the new file.
    slots[0] = {8U, true, 0xB0U};
    resolved = user::file_handle::resolve(slots, 2U, handle(0U, 7U));
    assert(resolved.resolution == Resolution::Stale);
    resolved = user::file_handle::resolve(slots, 2U, handle(0U, 8U));
    assert(resolved.resolution == Resolution::Ok);
    assert(resolved.slot == &slots[0]);
    assert(resolved.slot->value == 0xB0U);

    // Standard descriptors, values between namespaces and indices beyond the
    // table are malformed rather than stale.
    assert(user::file_handle::resolve(slots, 2U, UINT64_C(0)).resolution ==
        Resolution::Invalid);
    assert(user::file_handle::resolve(slots, 2U, UINT64_C(2)).resolution ==
        Resolution::Invalid);
    assert(user::file_handle::resolve(slots, 2U, handle(2U, 8U)).resolution ==
        Resolution::Invalid);
    assert(user::file_handle::resolve<TestSlot>(nullptr, 2U, handle(0U, 8U)).resolution ==
        Resolution::Invalid);

    // An in-range descriptor carrying any obsolete generation is stale.
    assert(user::file_handle::resolve(slots, 2U, handle(0U, 0U)).resolution ==
        Resolution::Stale);
    assert(user::file_handle::resolve(slots, 2U, handle(1U, 99U)).resolution ==
        Resolution::Stale);

    std::puts("file handle resolution tests passed");
    return 0;
}

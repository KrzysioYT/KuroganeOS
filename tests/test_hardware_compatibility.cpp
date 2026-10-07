#include "../kernel/hardware/compatibility.hpp"

#include <cassert>
#include <iostream>

int main() {
    using namespace hardware;
    using policy::CapabilityMask;

    const CapabilityMask boot =
        compatibility::BOOT_TIER;
    auto result = compatibility::evaluate(boot);
    assert(result.tier == compatibility::Tier::Boot);
    assert((result.missing_for_next_tier & policy::CapabilityKeyboard) != 0U);
    assert((result.missing_for_next_tier & policy::CapabilityStorage) != 0U);

    const CapabilityMask usable = compatibility::USABLE_TIER;
    result = compatibility::evaluate(usable);
    assert(result.tier == compatibility::Tier::Usable);
    assert((result.missing_for_next_tier & policy::CapabilityNetwork) != 0U);
    assert((result.missing_for_next_tier & policy::CapabilityAudio) != 0U);

    result = compatibility::evaluate(compatibility::CONNECTED_TIER);
    assert(result.tier == compatibility::Tier::Connected);
    assert((result.missing_for_next_tier &
            policy::CapabilityMultiprocessor) != 0U);

    result = compatibility::evaluate(compatibility::EXTENDED_TIER);
    assert(result.tier == compatibility::Tier::Extended);
    assert(result.missing_for_next_tier == policy::CapabilityNone);

    result = compatibility::evaluate(policy::CapabilityDisplay);
    assert(result.tier == compatibility::Tier::Unsupported);
    assert((result.missing_for_next_tier & policy::CapabilityTimer) != 0U);

    std::cout << "Hardware compatibility tiers: PASS\n";
    return 0;
}

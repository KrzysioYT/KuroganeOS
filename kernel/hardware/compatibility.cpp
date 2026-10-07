#include "compatibility.hpp"

namespace hardware::compatibility {
namespace {

bool satisfies(
    policy::CapabilityMask available,
    policy::CapabilityMask required_mask) {
    return (available & required_mask) == required_mask;
}

} // namespace

policy::CapabilityMask required(Tier tier) {
    switch (tier) {
        case Tier::Boot: return BOOT_TIER;
        case Tier::Usable: return USABLE_TIER;
        case Tier::Connected: return CONNECTED_TIER;
        case Tier::Extended: return EXTENDED_TIER;
        case Tier::Unsupported: return policy::CapabilityNone;
    }
    return policy::CapabilityNone;
}

Result evaluate(policy::CapabilityMask available) {
    Tier tier = Tier::Unsupported;
    policy::CapabilityMask next = BOOT_TIER;

    if (satisfies(available, BOOT_TIER)) {
        tier = Tier::Boot;
        next = USABLE_TIER;
    }
    if (satisfies(available, USABLE_TIER)) {
        tier = Tier::Usable;
        next = CONNECTED_TIER;
    }
    if (satisfies(available, CONNECTED_TIER)) {
        tier = Tier::Connected;
        next = EXTENDED_TIER;
    }
    if (satisfies(available, EXTENDED_TIER)) {
        tier = Tier::Extended;
        next = EXTENDED_TIER;
    }

    return {
        tier,
        available,
        next & ~available,
    };
}

const char* tier_name(Tier tier) {
    switch (tier) {
        case Tier::Unsupported: return "UNSUPPORTED";
        case Tier::Boot: return "TIER 0 BOOT";
        case Tier::Usable: return "TIER 1 USABLE";
        case Tier::Connected: return "TIER 2 CONNECTED";
        case Tier::Extended: return "TIER 3 EXTENDED";
    }
    return "UNKNOWN";
}

} // namespace hardware::compatibility

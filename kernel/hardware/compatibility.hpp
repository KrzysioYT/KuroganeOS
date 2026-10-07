#pragma once

#include <stdint.h>

#include "policy.hpp"

namespace hardware::compatibility {

enum class Tier : uint8_t {
    Unsupported = 0,
    Boot,
    Usable,
    Connected,
    Extended,
};

struct Result {
    Tier tier;
    policy::CapabilityMask available;
    policy::CapabilityMask missing_for_next_tier;
};

constexpr policy::CapabilityMask BOOT_TIER =
    policy::BOOT_CRITICAL_CAPABILITIES |
    policy::CapabilityDisplay;

constexpr policy::CapabilityMask USABLE_TIER =
    BOOT_TIER |
    policy::CapabilityKeyboard |
    policy::CapabilityPointer |
    policy::CapabilityStorage;

constexpr policy::CapabilityMask CONNECTED_TIER =
    USABLE_TIER |
    policy::CapabilityNetwork |
    policy::CapabilityAudio;

constexpr policy::CapabilityMask EXTENDED_TIER =
    CONNECTED_TIER |
    policy::CapabilityUsbHost |
    policy::CapabilityMultiprocessor;

Result evaluate(policy::CapabilityMask available);
policy::CapabilityMask required(Tier tier);
const char* tier_name(Tier tier);

} // namespace hardware::compatibility

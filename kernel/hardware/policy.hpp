#pragma once

#include <stdint.h>

namespace hardware::policy {

using CapabilityMask = uint32_t;

enum Capability : CapabilityMask {
    CapabilityNone = 0U,
    CapabilityTimer = UINT32_C(1) << 0U,
    CapabilitySchedulerInterrupt = UINT32_C(1) << 1U,
    CapabilityInputQueue = UINT32_C(1) << 2U,
    CapabilityLegacyKeyboard = UINT32_C(1) << 3U,
    CapabilityLegacyPointer = UINT32_C(1) << 4U,
    CapabilityUsbHost = UINT32_C(1) << 5U,
    CapabilityStorage = UINT32_C(1) << 6U,
    CapabilityNetwork = UINT32_C(1) << 7U,
    CapabilityAudio = UINT32_C(1) << 8U,
    CapabilityMultiprocessor = UINT32_C(1) << 9U,
};

constexpr CapabilityMask BOOT_CRITICAL_CAPABILITIES =
    CapabilityTimer |
    CapabilitySchedulerInterrupt |
    CapabilityInputQueue;

constexpr CapabilityMask OPTIONAL_HARDWARE_CAPABILITIES =
    CapabilityLegacyKeyboard |
    CapabilityLegacyPointer |
    CapabilityUsbHost |
    CapabilityStorage |
    CapabilityNetwork |
    CapabilityAudio |
    CapabilityMultiprocessor;

struct Evaluation {
    CapabilityMask available;
    CapabilityMask missing_required;
    CapabilityMask missing_optional;

    bool bootable() const { return missing_required == CapabilityNone; }
};

Evaluation evaluate(CapabilityMask available);
bool available(CapabilityMask mask, Capability capability);
bool boot_critical(Capability capability);
const char* capability_name(Capability capability);

} // namespace hardware::policy

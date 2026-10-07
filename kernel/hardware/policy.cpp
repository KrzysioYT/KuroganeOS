#include "policy.hpp"

namespace hardware::policy {

Evaluation evaluate(CapabilityMask available_mask) {
    return {
        available_mask,
        BOOT_CRITICAL_CAPABILITIES & ~available_mask,
        OPTIONAL_HARDWARE_CAPABILITIES & ~available_mask,
    };
}

bool available(CapabilityMask mask, Capability capability) {
    const CapabilityMask bit = static_cast<CapabilityMask>(capability);
    return bit != CapabilityNone && (mask & bit) == bit;
}

bool boot_critical(Capability capability) {
    const CapabilityMask bit = static_cast<CapabilityMask>(capability);
    return bit != CapabilityNone &&
        (BOOT_CRITICAL_CAPABILITIES & bit) == bit;
}

const char* capability_name(Capability capability) {
    switch (capability) {
        case CapabilityTimer: return "timer";
        case CapabilitySchedulerInterrupt: return "scheduler-interrupt";
        case CapabilityInputQueue: return "input-queue";
        case CapabilityLegacyKeyboard: return "legacy-keyboard";
        case CapabilityLegacyPointer: return "legacy-pointer";
        case CapabilityUsbHost: return "usb-host";
        case CapabilityStorage: return "storage";
        case CapabilityNetwork: return "network";
        case CapabilityAudio: return "audio";
        case CapabilityMultiprocessor: return "multiprocessor";
        case CapabilityNone: break;
    }
    return "unknown";
}

} // namespace hardware::policy

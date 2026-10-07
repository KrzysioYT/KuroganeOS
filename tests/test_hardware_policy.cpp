#include "../kernel/hardware/policy.hpp"

#include <cassert>
#include <cstring>

int main() {
    using namespace hardware::policy;

    const CapabilityMask core =
        CapabilityTimer |
        CapabilitySchedulerInterrupt |
        CapabilityInputQueue;

    const Evaluation portable = evaluate(core);
    assert(portable.bootable());
    assert(portable.missing_required == CapabilityNone);
    assert((portable.missing_optional & CapabilityLegacyKeyboard) != 0U);
    assert((portable.missing_optional & CapabilityLegacyPointer) != 0U);
    assert((portable.missing_optional & CapabilityNetwork) != 0U);

    const Evaluation usb_only_input = evaluate(
        core | CapabilityUsbHost | CapabilityStorage |
        CapabilityDisplay | CapabilityKeyboard | CapabilityPointer);
    assert(usb_only_input.bootable());
    assert(!available(usb_only_input.available, CapabilityLegacyKeyboard));
    assert(!available(usb_only_input.available, CapabilityLegacyPointer));
    assert(available(usb_only_input.available, CapabilityUsbHost));
    assert(available(usb_only_input.available, CapabilityKeyboard));
    assert(available(usb_only_input.available, CapabilityPointer));
    assert(available(usb_only_input.available, CapabilityDisplay));

    const Evaluation no_timer = evaluate(
        CapabilitySchedulerInterrupt | CapabilityInputQueue |
        CapabilityLegacyKeyboard | CapabilityLegacyPointer);
    assert(!no_timer.bootable());
    assert((no_timer.missing_required & CapabilityTimer) != 0U);

    assert(boot_critical(CapabilityTimer));
    assert(boot_critical(CapabilityInputQueue));
    assert(!boot_critical(CapabilityLegacyKeyboard));
    assert(!boot_critical(CapabilityNetwork));
    assert(std::strcmp(capability_name(CapabilityUsbHost), "usb-host") == 0);

    return 0;
}

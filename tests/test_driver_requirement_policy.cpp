#include "../kernel/drivers/core/device_manager.hpp"
#include "../kernel/drivers/core/driver_manager.hpp"

#include <cassert>

namespace {

constexpr uint16_t kVendor = 0x4B55;
constexpr uint16_t kOptionalDevice = 0x5101;
constexpr uint16_t kRequiredDevice = 0x5102;

bool match(const drivers::device::Device& device, void*) {
    return device.bus == drivers::device::Bus::Virtual &&
        device.vendor_id == kVendor;
}

KStatus probe(const drivers::device::Device&, uint32_t, void*) {
    return KStatus::Ok;
}

KStatus fail_attach(const drivers::device::Device&, uint32_t, void*) {
    return KStatus::IoError;
}

drivers::device::Descriptor descriptor(
    const char* name,
    uint16_t device_id,
    drivers::device::Requirement requirement) {
    return {
        drivers::device::Type::Input,
        drivers::device::Bus::Virtual,
        name,
        kVendor,
        device_id,
        0U,
        0U,
        0U,
        {0U, 0U, 0U, 0U},
        drivers::device::INVALID_DEVICE_ID,
        nullptr,
        0U,
        requirement,
    };
}

} // namespace

int main() {
    using namespace drivers;

    assert(device::initialize() == KStatus::Ok);
    assert(driver::initialize() == KStatus::Ok);

    device::DriverId driver_id = device::INVALID_DRIVER_ID;
    const driver::Descriptor failing_driver{
        "portability-failing-driver",
        100,
        32,
        match,
        probe,
        fail_attach,
        nullptr,
        nullptr,
    };
    assert(driver::register_driver(failing_driver, &driver_id) == KStatus::Ok);

    device::DeviceId optional_id = device::INVALID_DEVICE_ID;
    const auto optional = descriptor(
        "optional-device", kOptionalDevice, device::Requirement::Optional);
    assert(device::register_device(optional, &optional_id) == KStatus::Ok);

    // Optional hardware may fail to bind without turning bind_all() into a
    // global boot failure.
    assert(driver::bind_all() == KStatus::Ok);
    const device::Device* optional_state = device::get(optional_id);
    assert(optional_state != nullptr);
    assert(optional_state->status == device::Status::Failed);
    assert(optional_state->requirement == device::Requirement::Optional);

    device::DeviceId required_id = device::INVALID_DEVICE_ID;
    const auto required = descriptor(
        "required-device", kRequiredDevice,
        device::Requirement::BootCritical);
    assert(device::register_device(required, &required_id) == KStatus::Ok);

    // The same driver failure is propagated once the device is explicitly
    // classified as boot-critical.
    assert(driver::bind_all() == KStatus::IoError);
    const device::Device* required_state = device::get(required_id);
    assert(required_state != nullptr);
    assert(required_state->status == device::Status::Failed);
    assert(required_state->requirement == device::Requirement::BootCritical);

    assert(device::requirement_name(device::Requirement::Optional)[0] == 'O');
    assert(device::requirement_name(device::Requirement::BootCritical)[0] == 'B');
    return 0;
}

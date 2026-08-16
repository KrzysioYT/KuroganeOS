#include <cassert>
#include <cstdint>
#include <iostream>

#include "../kernel/drivers/core/device_manager.hpp"
#include "../kernel/drivers/core/driver_manager.hpp"

namespace {

struct Context {
    uint32_t expected_timeout;
    size_t probes;
    size_t attaches;
    KStatus probe_result;
    KStatus attach_result;
};

bool match_storage(const drivers::device::Device& device, void*) {
    return device.type == drivers::device::Type::StorageController;
}

KStatus probe(
    const drivers::device::Device&,
    uint32_t timeout,
    void* opaque) {
    auto* context = static_cast<Context*>(opaque);
    assert(timeout == context->expected_timeout);
    ++context->probes;
    return context->probe_result;
}

KStatus attach(
    const drivers::device::Device&,
    uint32_t timeout,
    void* opaque) {
    auto* context = static_cast<Context*>(opaque);
    assert(timeout == context->expected_timeout);
    ++context->attaches;
    return context->attach_result;
}

drivers::device::Descriptor descriptor(
    drivers::device::Type type,
    const char* name,
    drivers::device::DeviceId parent = drivers::device::INVALID_DEVICE_ID) {
    return {
        type,
        drivers::device::Bus::Pci,
        name,
        0x8086,
        0x2922,
        1,
        6,
        1,
        {0, 0, static_cast<uint8_t>(type), 0},
        parent,
        nullptr,
        0,
    };
}

} // namespace

int main() {
    using namespace drivers;
    assert(device::initialize() == KStatus::Ok);
    assert(driver::initialize() == KStatus::Ok);

    device::DeviceId storage = device::INVALID_DEVICE_ID;
    assert(device::register_device(
        descriptor(device::Type::StorageController, "SATA controller"),
        &storage) == KStatus::Ok);
    device::DeviceId disk = device::INVALID_DEVICE_ID;
    auto disk_descriptor = descriptor(device::Type::Block, "disk0", storage);
    disk_descriptor.bus_address.slot = 20;
    assert(device::register_device(disk_descriptor, &disk) == KStatus::Ok);
    assert(device::get(storage)->child_count == 1);
    assert(device::get(storage)->children[0] == disk);

    Context preferred{25, 0, 0, KStatus::NotSupported, KStatus::Ok};
    Context fallback{50, 0, 0, KStatus::Ok, KStatus::Ok};
    device::DriverId preferred_id = device::INVALID_DRIVER_ID;
    device::DriverId fallback_id = device::INVALID_DRIVER_ID;
    assert(driver::register_driver(
        {"preferred", 100, 25, match_storage, probe, attach, nullptr, &preferred},
        &preferred_id) == KStatus::Ok);
    assert(driver::register_driver(
        {"fallback", 10, 50, match_storage, probe, attach, nullptr, &fallback},
        &fallback_id) == KStatus::Ok);
    assert(driver::bind_device(storage) == KStatus::Ok);
    assert(preferred.probes == 1 && preferred.attaches == 0);
    assert(fallback.probes == 1 && fallback.attaches == 1);
    assert(device::get(storage)->driver == fallback_id);
    assert(device::get(storage)->status == device::Status::Ready);
    assert(device::claim(storage, preferred_id, "preferred") == KStatus::Busy);
    assert(driver::bind_device(disk) == KStatus::NotFound);
    assert(device::get(disk)->status == device::Status::Discovered);

    assert(driver::find("fallback") != nullptr);
    assert(driver::find("fallback")->attached_count == 1);
    assert(driver::register_driver(
        {"fallback", 0, 1, match_storage, probe, attach, nullptr, &fallback},
        &preferred_id) == KStatus::AlreadyExists);

    std::cout << "driver core tests: PASS\n";
    return 0;
}

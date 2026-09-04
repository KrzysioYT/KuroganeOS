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
    size_t detaches;
    KStatus probe_result;
    KStatus attach_result;
    bool partial_active;
};

bool match_storage(const drivers::device::Device& device, void*) {
    return device.type == drivers::device::Type::StorageController;
}

bool match_input(const drivers::device::Device& device, void*) {
    return device.type == drivers::device::Type::Input;
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
    context->partial_active = true;
    return context->attach_result;
}

void detach(const drivers::device::Device&, void* opaque) {
    auto* context = static_cast<Context*>(opaque);
    ++context->detaches;
    context->partial_active = false;
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

drivers::device::Descriptor virtual_descriptor(
    const char* name,
    const drivers::device::Resource* resources,
    size_t resource_count,
    drivers::device::DeviceId parent = drivers::device::INVALID_DEVICE_ID) {
    return {
        drivers::device::Type::Input,
        drivers::device::Bus::Virtual,
        name,
        0,
        0,
        0,
        0,
        0,
        {0, 0, 0, 0},
        parent,
        resources,
        resource_count,
    };
}

struct ReentrantContext {
    drivers::device::DeviceId original;
    drivers::device::DeviceId replacement;
    size_t probes;
    size_t attaches;
};

KStatus replace_during_probe(
    const drivers::device::Device&,
    uint32_t,
    void* opaque) {
    auto* context = static_cast<ReentrantContext*>(opaque);
    ++context->probes;
    assert(drivers::device::remove_device(context->original) == KStatus::Ok);
    assert(drivers::device::register_device(
        virtual_descriptor("probe-replacement", nullptr, 0),
        &context->replacement) == KStatus::Ok);
    return KStatus::Ok;
}

KStatus count_reentrant_attach(
    const drivers::device::Device&,
    uint32_t,
    void* opaque) {
    ++static_cast<ReentrantContext*>(opaque)->attaches;
    return KStatus::Ok;
}

struct ReentrantDetachContext {
    drivers::device::DeviceId replacement;
    size_t detaches;
};

KStatus accept_lifecycle(
    const drivers::device::Device&,
    uint32_t,
    void*) {
    return KStatus::Ok;
}

void replace_during_detach(
    const drivers::device::Device& target,
    void* opaque) {
    auto* context = static_cast<ReentrantDetachContext*>(opaque);
    ++context->detaches;
    assert(drivers::device::release(target.id, target.driver) == KStatus::Ok);
    assert(drivers::device::remove_device(target.id) == KStatus::Ok);
    assert(drivers::device::register_device(
        virtual_descriptor("detach-replacement", nullptr, 0),
        &context->replacement) == KStatus::Ok);
}

} // namespace

int main() {
    using namespace drivers;
    assert(device::initialize() == KStatus::Ok);
    assert(driver::initialize() == KStatus::Ok);

    device::DeviceId storage = device::INVALID_DEVICE_ID;
    const size_t initial_count = device::count();
    const size_t initial_active_count = device::active_count();
    assert(device::register_device(
        descriptor(device::Type::StorageController, "SATA controller"),
        &storage) == KStatus::Ok);
    assert(device::count() == initial_count + 1U);
    assert(device::active_count() == initial_active_count + 1U);
    device::DeviceId disk = device::INVALID_DEVICE_ID;
    auto disk_descriptor = descriptor(device::Type::Block, "disk0", storage);
    disk_descriptor.bus_address.slot = 20;
    assert(device::register_device(disk_descriptor, &disk) == KStatus::Ok);
    assert(device::get(storage)->child_count == 1);
    assert(device::get(storage)->children[0] == disk);

    device::Resource virtual_resource{device::ResourceType::Mmio, 0x1000, 0x100, 0};
    device::DeviceId virtual_parent = device::INVALID_DEVICE_ID;
    assert(device::register_device(
        virtual_descriptor("virtual-input", &virtual_resource, 1),
        &virtual_parent) == KStatus::Ok);
    const device::DeviceHandle stale_handle = device::handle_for(virtual_parent);
    assert(device::has_capability(stale_handle, device::CapabilityMmio));
    assert(device::has_capability(stale_handle, device::CapabilityHotRemove));
    device::Resource returned_resource{};
    assert(device::get_resource(stale_handle, 0, &returned_resource) == KStatus::Ok);
    assert(returned_resource.type == device::ResourceType::Mmio &&
        returned_resource.start == virtual_resource.start &&
        returned_resource.length == virtual_resource.length);
    assert(device::get_resource(stale_handle, 1, &returned_resource) == KStatus::OutOfRange);
    const uint32_t initial_lifecycle = device::get(virtual_parent)->lifecycle_generation;
    assert(device::set_status(virtual_parent, device::Status::Probing) == KStatus::Ok);
    assert(device::get(virtual_parent)->lifecycle_generation == initial_lifecycle + 1U);
    assert(device::set_status(virtual_parent, device::Status::Probing) == KStatus::Ok);
    assert(device::get(virtual_parent)->lifecycle_generation == initial_lifecycle + 1U);

    device::DeviceId virtual_child = device::INVALID_DEVICE_ID;
    assert(device::register_device(
        virtual_descriptor("virtual-child", nullptr, 0, virtual_parent),
        &virtual_child) == KStatus::Ok);
    assert(device::get(virtual_parent)->child_count == 1);
    assert(device::remove_device(virtual_parent) == KStatus::Busy);
    assert(device::remove_device(virtual_child) == KStatus::Ok);
    assert(device::get(virtual_parent)->child_count == 0);
    assert(device::remove_device(virtual_parent) == KStatus::Ok);
    assert(device::resolve(stale_handle) == nullptr);
    assert(device::active_count() == initial_active_count + 2U);

    device::DeviceId reused = device::INVALID_DEVICE_ID;
    assert(device::register_device(
        virtual_descriptor("virtual-reused", nullptr, 0), &reused) == KStatus::Ok);
    assert(reused == virtual_parent);
    const device::DeviceHandle reused_handle = device::handle_for(reused);
    assert(reused_handle != stale_handle);
    assert(device::resolve(stale_handle) == nullptr);
    assert(device::resolve(reused_handle) != nullptr);
    assert(device::count() == initial_count + 4U);
    assert(device::active_count() == initial_active_count + 3U);
    const uint32_t claim_lifecycle = device::get(reused)->lifecycle_generation;
    const device::DeviceHandle unclaimed_handle = device::handle_for(reused);
    assert(device::claim(reused, 77U, "test-driver") == KStatus::Ok);
    assert(device::get(reused)->lifecycle_generation == claim_lifecycle + 1U);
    const device::DeviceHandle claimed_handle = device::handle_for(reused);
    assert(claimed_handle != unclaimed_handle);
    assert(device::resolve(unclaimed_handle) == nullptr);
    assert(device::resolve(claimed_handle) != nullptr);
    assert(device::release(reused, 77U) == KStatus::Ok);
    assert(device::get(reused)->lifecycle_generation == claim_lifecycle + 2U);
    const device::DeviceHandle released_handle = device::handle_for(reused);
    assert(released_handle != claimed_handle);
    assert(device::resolve(claimed_handle) == nullptr);
    assert(device::resolve(released_handle) != nullptr);

    Context preferred{25, 0, 0, 0, KStatus::NotSupported, KStatus::Ok, false};
    Context fallback{50, 0, 0, 0, KStatus::Ok, KStatus::Ok, false};
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
    assert(device::remove_device(storage) == KStatus::Busy);
    assert(device::claim(storage, preferred_id, "preferred") == KStatus::Busy);
    assert(driver::bind_device(disk) == KStatus::NotFound);
    assert(device::get(disk)->status == device::Status::Discovered);

    assert(driver::find("fallback") != nullptr);
    assert(driver::find("fallback")->attached_count == 1);
    assert(driver::register_driver(
        {"fallback", 0, 1, match_storage, probe, attach, nullptr, &fallback},
        &preferred_id) == KStatus::AlreadyExists);

    Context failing_input{10, 0, 0, 0, KStatus::Ok, KStatus::IoError, false};
    Context fallback_input{10, 0, 0, 0, KStatus::Ok, KStatus::Ok, false};
    device::DriverId failing_input_id = device::INVALID_DRIVER_ID;
    device::DriverId fallback_input_id = device::INVALID_DRIVER_ID;
    assert(driver::register_driver(
        {"input-failing", 100, 10, match_input, probe, attach, detach, &failing_input},
        &failing_input_id) == KStatus::Ok);
    assert(driver::register_driver(
        {"input-fallback", 50, 10, match_input, probe, attach, detach, &fallback_input},
        &fallback_input_id) == KStatus::Ok);
    assert(driver::bind_device(reused) == KStatus::Ok);
    assert(device::get(reused)->driver == fallback_input_id);
    assert(failing_input.probes == 1 && failing_input.attaches == 1);
    assert(failing_input.detaches == 1U && !failing_input.partial_active);
    assert(fallback_input.probes == 1 && fallback_input.attaches == 1);
    assert(fallback_input.partial_active);
    assert(driver::get(failing_input_id)->failure_count == 1);
    assert(driver::get(failing_input_id)->last_failure_stage == driver::FailureStage::Attach);
    assert(driver::get(failing_input_id)->attached_count == 0);
    assert(driver::report_device_failure(reused, KStatus::IoError) == KStatus::Ok);
    assert(fallback_input.detaches == 1U);
    assert(!fallback_input.partial_active);
    assert(device::get(reused)->driver == device::INVALID_DRIVER_ID);
    assert(device::get(reused)->status == device::Status::Failed);
    assert(driver::rebind_device(reused) == KStatus::Ok);
    assert(device::get(reused)->driver == fallback_input_id);
    assert(failing_input.probes == 2 && failing_input.attaches == 2);
    assert(failing_input.detaches == 2U && !failing_input.partial_active);
    assert(fallback_input.probes == 2 && fallback_input.attaches == 2);
    assert(fallback_input.partial_active);
    assert(driver::get(failing_input_id)->failure_count == 2);

    const device::DeviceHandle bound_handle = device::handle_for(reused);
    assert(driver::unbind_device(reused) == KStatus::Ok);
    assert(fallback_input.detaches == 2U);
    assert(!fallback_input.partial_active);
    assert(device::resolve(bound_handle) == nullptr);
    const device::DeviceHandle unbound_handle = device::handle_for(reused);
    assert(device::resolve(unbound_handle) != nullptr);
    assert(device::get(reused)->driver == device::INVALID_DRIVER_ID);
    assert(device::get(reused)->status == device::Status::Discovered);
    assert(driver::get(fallback_input_id)->attached_count == 0U);
    assert(device::remove_device(reused) == KStatus::Ok);
    assert(device::resolve(unbound_handle) == nullptr);
    device::DeviceId lifecycle_replacement = device::INVALID_DEVICE_ID;
    assert(device::register_device(
        virtual_descriptor("lifecycle-replacement", nullptr, 0),
        &lifecycle_replacement) == KStatus::Ok);
    assert(lifecycle_replacement == reused);
    assert(device::handle_for(lifecycle_replacement) != unbound_handle);

    // A probe may synchronously trigger hot removal. A replacement can reuse
    // the numeric slot, but it must never inherit the in-flight bind.
    assert(device::initialize() == KStatus::Ok);
    assert(driver::initialize() == KStatus::Ok);
    ReentrantContext reentrant{
        device::INVALID_DEVICE_ID,
        device::INVALID_DEVICE_ID,
        0,
        0,
    };
    assert(device::register_device(
        virtual_descriptor("probe-original", nullptr, 0),
        &reentrant.original) == KStatus::Ok);
    const device::DeviceHandle removed_handle = device::handle_for(reentrant.original);
    device::DriverId reentrant_driver = device::INVALID_DRIVER_ID;
    assert(driver::register_driver(
        {"reentrant-probe", 100, 10, match_input, replace_during_probe,
            count_reentrant_attach, nullptr, &reentrant},
        &reentrant_driver) == KStatus::Ok);
    assert(driver::bind_device(reentrant.original) == KStatus::NoDevice);
    assert(reentrant.probes == 1U && reentrant.attaches == 0U);
    assert(reentrant.replacement == reentrant.original);
    assert(device::resolve(removed_handle) == nullptr);
    assert(device::get(reentrant.replacement)->driver == device::INVALID_DRIVER_ID);
    assert(device::get(reentrant.replacement)->status == device::Status::Discovered);

    assert(device::initialize() == KStatus::Ok);
    assert(driver::initialize() == KStatus::Ok);
    device::DeviceId detach_original = device::INVALID_DEVICE_ID;
    assert(device::register_device(
        virtual_descriptor("detach-original", nullptr, 0),
        &detach_original) == KStatus::Ok);
    ReentrantDetachContext reentrant_detach{device::INVALID_DEVICE_ID, 0};
    device::DriverId detach_driver = device::INVALID_DRIVER_ID;
    assert(driver::register_driver(
        {"reentrant-detach", 100, 10, match_input, accept_lifecycle,
            accept_lifecycle, replace_during_detach, &reentrant_detach},
        &detach_driver) == KStatus::Ok);
    assert(driver::bind_device(detach_original) == KStatus::Ok);
    const device::DeviceHandle detached_handle = device::handle_for(detach_original);
    assert(driver::get(detach_driver)->attached_count == 1U);
    assert(driver::unbind_device(detach_original) == KStatus::NoDevice);
    assert(reentrant_detach.detaches == 1U);
    assert(reentrant_detach.replacement == detach_original);
    assert(device::resolve(detached_handle) == nullptr);
    assert(device::get(reentrant_detach.replacement)->driver ==
        device::INVALID_DRIVER_ID);
    assert(device::get(reentrant_detach.replacement)->status ==
        device::Status::Discovered);
    assert(driver::get(detach_driver)->attached_count == 0U);
    assert(driver::get(detach_driver)->status == driver::Status::Registered);

    std::cout << "driver core tests: PASS\n";
    return 0;
}

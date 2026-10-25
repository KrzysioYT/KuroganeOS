#include "runtime_qualification.hpp"

namespace drivers::runtime_qualification {
namespace {

constexpr uint16_t kQualificationVendor = UINT16_C(0x4B55);
constexpr uint16_t kTargetDevice = UINT16_C(0x4001);
constexpr uint16_t kSurvivorDevice = UINT16_C(0x4002);
constexpr uint32_t kTimeoutTicks = 32U;

struct State {
    device::DeviceId target;
    device::DeviceId survivor;
    device::DriverId primary_driver;
    device::DriverId fallback_driver;
    size_t primary_probes;
    size_t primary_attaches;
    size_t primary_detaches;
    size_t fallback_probes;
    size_t fallback_attaches;
    size_t fallback_detaches;
    bool primary_partial_active;
    bool fallback_active;
    bool executed;
};

State g_state{};

bool match_target(const device::Device& target, void*) {
    return target.bus == device::Bus::Virtual &&
        target.vendor_id == kQualificationVendor &&
        target.device_id == kTargetDevice;
}

KStatus primary_probe(
    const device::Device& target,
    uint32_t timeout_ticks,
    void*) {
    ++g_state.primary_probes;
    return timeout_ticks == kTimeoutTicks && match_target(target, nullptr)
        ? KStatus::Ok : KStatus::NotSupported;
}

KStatus primary_attach(
    const device::Device& target,
    uint32_t timeout_ticks,
    void*) {
    ++g_state.primary_attaches;
    if (timeout_ticks != kTimeoutTicks || !match_target(target, nullptr)) {
        return KStatus::InvalidArgument;
    }
    g_state.primary_partial_active = true;
    return KStatus::IoError;
}

void primary_detach(const device::Device&, void*) {
    ++g_state.primary_detaches;
    g_state.primary_partial_active = false;
}

KStatus fallback_probe(
    const device::Device& target,
    uint32_t timeout_ticks,
    void*) {
    ++g_state.fallback_probes;
    return timeout_ticks == kTimeoutTicks && match_target(target, nullptr)
        ? KStatus::Ok : KStatus::NotSupported;
}

KStatus fallback_attach(
    const device::Device& target,
    uint32_t timeout_ticks,
    void*) {
    ++g_state.fallback_attaches;
    if (timeout_ticks != kTimeoutTicks || !match_target(target, nullptr)) {
        return KStatus::InvalidArgument;
    }
    g_state.fallback_active = true;
    return KStatus::Ok;
}

void fallback_detach(const device::Device&, void*) {
    ++g_state.fallback_detaches;
    g_state.fallback_active = false;
}

device::Descriptor virtual_descriptor(
    device::Type type,
    const char* name,
    uint16_t device_id,
    device::DeviceId parent,
    const device::Resource* resources,
    size_t resource_count) {
    return {
        type,
        device::Bus::Virtual,
        name,
        kQualificationVendor,
        device_id,
        0,
        0,
        0,
        {0, 0, 0, 0},
        parent,
        resources,
        resource_count,
    };
}

void cleanup_devices() {
    device::Device* target = device::get_mutable(g_state.target);
    if (target != nullptr && target->driver != device::INVALID_DRIVER_ID) {
        static_cast<void>(driver::unbind_device(g_state.target));
    }
    target = device::get_mutable(g_state.target);
    if (target != nullptr && target->driver == device::INVALID_DRIVER_ID &&
        target->child_count == 0U) {
        static_cast<void>(device::remove_device(g_state.target));
    }
    device::Device* survivor = device::get_mutable(g_state.survivor);
    if (survivor != nullptr && survivor->driver == device::INVALID_DRIVER_ID &&
        survivor->child_count == 0U) {
        static_cast<void>(device::remove_device(g_state.survivor));
    }
}

KStatus fail(KStatus status) {
    cleanup_devices();
    return status;
}

} // namespace

KStatus run(Result* output) {
    if (output == nullptr) return KStatus::InvalidArgument;
    *output = {};
    if (!device::initialized() || !driver::initialized()) {
        return KStatus::BadState;
    }
    if (g_state.executed) return KStatus::AlreadyExists;
    g_state = {};
    g_state.executed = true;
    g_state.target = device::INVALID_DEVICE_ID;
    g_state.survivor = device::INVALID_DEVICE_ID;
    g_state.primary_driver = device::INVALID_DRIVER_ID;
    g_state.fallback_driver = device::INVALID_DRIVER_ID;

    const size_t initial_active_count = device::active_count();
    const driver::Descriptor primary{
        "presteel-primary",
        200,
        kTimeoutTicks,
        match_target,
        primary_probe,
        primary_attach,
        primary_detach,
        nullptr,
    };
    KStatus status = driver::register_driver(primary, &g_state.primary_driver);
    if (status != KStatus::Ok) return status;
    const driver::Descriptor fallback{
        "presteel-fallback",
        100,
        kTimeoutTicks,
        match_target,
        fallback_probe,
        fallback_attach,
        fallback_detach,
        nullptr,
    };
    status = driver::register_driver(fallback, &g_state.fallback_driver);
    if (status != KStatus::Ok) return status;

    const device::Descriptor survivor_descriptor = virtual_descriptor(
        device::Type::Platform,
        "presteel-survivor",
        kSurvivorDevice,
        device::INVALID_DEVICE_ID,
        nullptr,
        0U);
    status = device::register_device(survivor_descriptor, &g_state.survivor);
    if (status != KStatus::Ok) return status;
    const device::DeviceHandle survivor_handle =
        device::handle_for(g_state.survivor);

    const device::Resource resources[] = {
        {device::ResourceType::Mmio, UINT64_C(0xFEC10000), UINT64_C(0x1000), 0U},
        {device::ResourceType::Irq, UINT64_C(23), UINT64_C(1), 0U},
    };
    const device::Descriptor target_descriptor = virtual_descriptor(
        device::Type::Input,
        "presteel-runtime-device",
        kTargetDevice,
        g_state.survivor,
        resources,
        2U);
    status = device::register_device(target_descriptor, &g_state.target);
    if (status != KStatus::Ok) return fail(status);
    const device::DeviceHandle discovered_handle =
        device::handle_for(g_state.target);
    const device::Device* survivor = device::resolve(survivor_handle);
    if (survivor == nullptr || survivor->child_count != 1U ||
        survivor->children[0] != g_state.target) {
        return fail(KStatus::Corrupted);
    }

    status = driver::bind_device(g_state.target);
    if (status != KStatus::Ok) return fail(status);
    const device::DeviceHandle first_bound_handle =
        device::handle_for(g_state.target);
    const device::Device* target = device::resolve(first_bound_handle);
    const driver::Driver* primary_state = driver::get(g_state.primary_driver);
    const driver::Driver* fallback_state = driver::get(g_state.fallback_driver);
    if (target == nullptr || primary_state == nullptr || fallback_state == nullptr) {
        return fail(KStatus::Corrupted);
    }

    device::Resource resource{};
    const bool resources_ok =
        device::has_capability(
            first_bound_handle,
            device::CapabilityMmio | device::CapabilityInterrupt |
                device::CapabilityHotRemove | device::CapabilityDriverBinding) &&
        device::get_resource(first_bound_handle, 0U, &resource) == KStatus::Ok &&
        resource.type == device::ResourceType::Mmio &&
        resource.start == resources[0].start &&
        resource.length == resources[0].length &&
        device::get_resource(first_bound_handle, 2U, &resource) ==
            KStatus::OutOfRange;
    output->device_resource_boundary = resources_ok;
    output->device_claim = target->driver == g_state.fallback_driver &&
        target->status == device::Status::Ready;
    output->device_generation =
        discovered_handle != first_bound_handle &&
        device::resolve(discovered_handle) == nullptr;
    output->driver_match = g_state.primary_probes == 1U &&
        g_state.fallback_probes == 1U;
    output->driver_attach = g_state.primary_attaches == 1U &&
        g_state.primary_detaches == 1U &&
        !g_state.primary_partial_active &&
        g_state.fallback_attaches == 1U && g_state.fallback_active &&
        fallback_state->attached_count == 1U;
    output->driver_fallback = primary_state->failure_count == 1U &&
        primary_state->last_failure == KStatus::IoError &&
        primary_state->last_failure_stage == driver::FailureStage::Attach &&
        target->driver == g_state.fallback_driver;

    status = driver::unbind_device(g_state.target);
    if (status != KStatus::Ok) return fail(status);
    const device::DeviceHandle unbound_handle =
        device::handle_for(g_state.target);
    target = device::resolve(unbound_handle);
    fallback_state = driver::get(g_state.fallback_driver);
    output->device_unbind = target != nullptr &&
        target->driver == device::INVALID_DRIVER_ID &&
        target->status == device::Status::Discovered &&
        device::resolve(first_bound_handle) == nullptr &&
        g_state.fallback_detaches == 1U && !g_state.fallback_active &&
        fallback_state != nullptr && fallback_state->attached_count == 0U;

    status = driver::rebind_device(g_state.target);
    if (status != KStatus::Ok) return fail(status);
    const device::DeviceHandle before_failure_handle =
        device::handle_for(g_state.target);
    status = driver::report_device_failure(g_state.target, KStatus::DeviceFault);
    if (status != KStatus::Ok) return fail(status);
    const device::DeviceHandle failed_handle = device::handle_for(g_state.target);
    target = device::resolve(failed_handle);
    fallback_state = driver::get(g_state.fallback_driver);
    survivor = device::resolve(survivor_handle);
    output->device_failure_isolation = target != nullptr && survivor != nullptr &&
        survivor->child_count == 1U &&
        target->driver == device::INVALID_DRIVER_ID &&
        target->status == device::Status::Failed &&
        device::resolve(before_failure_handle) == nullptr &&
        fallback_state != nullptr && fallback_state->failure_count == 1U &&
        fallback_state->last_failure == KStatus::DeviceFault &&
        fallback_state->last_failure_stage == driver::FailureStage::Runtime;
    output->driver_failure_cleanup = !g_state.primary_partial_active &&
        !g_state.fallback_active && fallback_state != nullptr &&
        fallback_state->attached_count == 0U;

    status = driver::rebind_device(g_state.target);
    if (status != KStatus::Ok) return fail(status);
    const device::DeviceHandle rebound_handle =
        device::handle_for(g_state.target);
    target = device::resolve(rebound_handle);
    output->device_rebind = target != nullptr &&
        target->driver == g_state.fallback_driver &&
        target->status == device::Status::Ready &&
        g_state.primary_probes == 3U && g_state.primary_attaches == 3U &&
        g_state.primary_detaches == 3U &&
        g_state.fallback_probes == 3U && g_state.fallback_attaches == 3U;

    status = driver::unbind_device(g_state.target);
    if (status != KStatus::Ok) return fail(status);
    const device::DeviceHandle removed_handle =
        device::handle_for(g_state.target);
    status = device::remove_device(g_state.target);
    if (status != KStatus::Ok) return fail(status);
    survivor = device::resolve(survivor_handle);
    const device::Device* stale_device = nullptr;
    const bool removed_handle_is_typed_stale =
        device::resolve_checked(removed_handle, &stale_device) ==
            KStatus::StaleHandle &&
        stale_device == nullptr;
    const bool first_remove_ok = device::resolve(removed_handle) == nullptr &&
        removed_handle_is_typed_stale && survivor != nullptr &&
        survivor->child_count == 0U &&
        device::active_count() == initial_active_count + 1U;

    device::DeviceId replacement = device::INVALID_DEVICE_ID;
    status = device::register_device(target_descriptor, &replacement);
    if (status != KStatus::Ok) return fail(status);
    const device::DeviceHandle replacement_handle = device::handle_for(replacement);
    const bool reuse_ok = replacement == g_state.target &&
        replacement_handle != removed_handle &&
        device::resolve(discovered_handle) == nullptr &&
        device::resolve(first_bound_handle) == nullptr &&
        device::resolve(unbound_handle) == nullptr &&
        device::resolve(before_failure_handle) == nullptr &&
        device::resolve(failed_handle) == nullptr &&
        device::resolve(rebound_handle) == nullptr &&
        device::resolve(removed_handle) == nullptr;
    g_state.target = replacement;
    status = device::remove_device(g_state.target);
    if (status != KStatus::Ok) return fail(status);
    survivor = device::resolve(survivor_handle);
    const bool replacement_remove_ok = survivor != nullptr &&
        survivor->child_count == 0U &&
        device::active_count() == initial_active_count + 1U;
    status = device::remove_device(g_state.survivor);
    if (status != KStatus::Ok) return fail(status);

    output->device_remove_cleanup = first_remove_ok && reuse_ok &&
        replacement_remove_ok && device::active_count() == initial_active_count;
    stale_device = nullptr;
    const bool replacement_handle_is_typed_stale =
        device::resolve_checked(replacement_handle, &stale_device) ==
            KStatus::StaleHandle &&
        stale_device == nullptr;
    output->device_stale_handle = reuse_ok && removed_handle_is_typed_stale &&
        replacement_handle_is_typed_stale &&
        device::resolve(replacement_handle) == nullptr &&
        device::resolve(survivor_handle) == nullptr;
    return output->complete() ? KStatus::Ok : KStatus::Corrupted;
}

} // namespace drivers::runtime_qualification

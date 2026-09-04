#include "driver_manager.hpp"

extern "C" KStatus kurogane_register_builtin_drivers() __attribute__((weak));

namespace drivers::driver {
namespace {

Driver g_drivers[MAXIMUM_DRIVERS]{};
size_t g_count = 0;
bool g_initialized = false;

void copy_text(char* destination, size_t capacity, const char* source) {
    size_t length = 0;
    while (length + 1 < capacity && source[length] != '\0') {
        destination[length] = source[length];
        ++length;
    }
    destination[length] = '\0';
}

bool text_equal(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) {
        return left == right;
    }
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}


void bump_lifecycle(Driver& driver) {
    ++driver.lifecycle_generation;
    if (driver.lifecycle_generation == 0U) driver.lifecycle_generation = 1U;
}

void record_failure(
    Driver& driver,
    device::DeviceId device_id,
    FailureStage stage,
    KStatus reason) {
    ++driver.failure_count;
    driver.last_failure = reason;
    driver.last_failure_device = device_id;
    driver.last_failure_stage = stage;
    driver.status = Status::Degraded;
    bump_lifecycle(driver);
}

} // namespace

KStatus initialize() {
    for (size_t index = 0; index < MAXIMUM_DRIVERS; ++index) {
        g_drivers[index] = {};
    }
    g_count = 0;
    g_initialized = true;

    // Hardware backends that are not part of the early boot-critical path can
    // register themselves here.  The hook runs before PCI Device records are
    // published, which is intentional: bind_all() happens only after the
    // complete device enumeration in kernel/main.cpp.
    if (kurogane_register_builtin_drivers != nullptr) {
        const KStatus status = kurogane_register_builtin_drivers();
        if (status != KStatus::Ok && status != KStatus::AlreadyExists) {
            g_initialized = false;
            g_count = 0;
            return status;
        }
    }
    return KStatus::Ok;
}

bool initialized() { return g_initialized; }
size_t count() { return g_count; }

KStatus register_driver(const Descriptor& descriptor, device::DriverId* id) {
    if (!g_initialized) {
        return KStatus::BadState;
    }
    if (id == nullptr || descriptor.name == nullptr ||
        descriptor.name[0] == '\0' || descriptor.timeout_ticks == 0 ||
        descriptor.match == nullptr || descriptor.probe == nullptr ||
        descriptor.attach == nullptr) {
        return KStatus::InvalidArgument;
    }
    *id = device::INVALID_DRIVER_ID;
    if (find(descriptor.name) != nullptr) {
        return KStatus::AlreadyExists;
    }
    if (g_count >= MAXIMUM_DRIVERS) {
        return KStatus::NoMemory;
    }
    Driver value{};
    value.id = static_cast<device::DriverId>(g_count);
    copy_text(value.name, sizeof(value.name), descriptor.name);
    value.priority = descriptor.priority;
    value.timeout_ticks = descriptor.timeout_ticks;
    value.match = descriptor.match;
    value.probe = descriptor.probe;
    value.attach = descriptor.attach;
    value.detach = descriptor.detach;
    value.context = descriptor.context;
    value.status = Status::Registered;
    g_drivers[g_count] = value;
    *id = value.id;
    ++g_count;
    return KStatus::Ok;
}

const Driver* get(device::DriverId id) {
    return id < g_count ? &g_drivers[id] : nullptr;
}

const Driver* find(const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    for (size_t index = 0; index < g_count; ++index) {
        if (text_equal(g_drivers[index].name, name)) {
            return &g_drivers[index];
        }
    }
    return nullptr;
}

KStatus bind_device(device::DeviceId id) {
    if (!g_initialized || !device::initialized()) {
        return KStatus::BadState;
    }
    device::DeviceHandle target_handle = device::handle_for(id);
    device::Device* target = device::resolve_mutable(target_handle);
    if (target == nullptr) {
        return KStatus::NotFound;
    }
    if (target->driver != device::INVALID_DRIVER_ID) {
        return KStatus::AlreadyExists;
    }

    bool tried[MAXIMUM_DRIVERS]{};
    bool matched = false;
    KStatus last_failure = KStatus::NotSupported;
    for (size_t attempt = 0; attempt < g_count; ++attempt) {
        size_t selected = MAXIMUM_DRIVERS;
        for (size_t index = 0; index < g_count; ++index) {
            Driver& candidate = g_drivers[index];
            if (tried[index]) {
                continue;
            }
            const bool matches = candidate.match(*target, candidate.context);
            target = device::resolve_mutable(target_handle);
            if (target == nullptr) return KStatus::NoDevice;
            if (!matches) continue;
            if (selected == MAXIMUM_DRIVERS ||
                candidate.priority > g_drivers[selected].priority) {
                selected = index;
            }
        }
        if (selected == MAXIMUM_DRIVERS) {
            break;
        }
        matched = true;
        tried[selected] = true;
        Driver& driver = g_drivers[selected];
        ++driver.probe_count;
        static_cast<void>(device::set_status(id, device::Status::Probing));
        KStatus status = driver.probe(*target, driver.timeout_ticks, driver.context);
        target = device::resolve_mutable(target_handle);
        if (target == nullptr) return KStatus::NoDevice;
        if (status != KStatus::Ok) {
            record_failure(driver, id, FailureStage::Probe, status);
            last_failure = status;
            static_cast<void>(device::set_status(id, device::Status::Discovered));
            continue;
        }

        status = device::claim(id, driver.id, driver.name);
        if (status != KStatus::Ok) {
            return status;
        }
        target_handle = device::handle_for(id);
        target = device::resolve_mutable(target_handle);
        if (target == nullptr || target->driver != driver.id) {
            return KStatus::NoDevice;
        }
        static_cast<void>(device::set_status(id, device::Status::Initializing));
        status = driver.attach(*target, driver.timeout_ticks, driver.context);
        target = device::resolve_mutable(target_handle);
        if (target == nullptr || target->driver != driver.id) {
            return KStatus::NoDevice;
        }
        if (status == KStatus::Ok) {
            ++driver.attached_count;
            driver.status = Status::Ready;
            driver.last_failure = KStatus::Ok;
            driver.last_failure_device = device::INVALID_DEVICE_ID;
            driver.last_failure_stage = FailureStage::None;
            bump_lifecycle(driver);
            static_cast<void>(device::set_status(id, device::Status::Ready));
            return KStatus::Ok;
        }
        record_failure(driver, id, FailureStage::Attach, status);
        last_failure = status;
        const KStatus release_status = device::release(id, driver.id);
        if (release_status != KStatus::Ok) return release_status;
        target_handle = device::handle_for(id);
        target = device::resolve_mutable(target_handle);
        if (target == nullptr) return KStatus::NoDevice;
    }

    if (matched) {
        if (device::resolve(target_handle) == nullptr) return KStatus::NoDevice;
        static_cast<void>(device::set_status(id, device::Status::Failed));
        return last_failure;
    }
    return KStatus::NotFound;
}

KStatus unbind_device(device::DeviceId id) {
    if (!g_initialized || !device::initialized()) return KStatus::BadState;
    const device::DeviceHandle target_handle = device::handle_for(id);
    device::Device* target = device::resolve_mutable(target_handle);
    if (target == nullptr) return KStatus::NotFound;
    if (target->driver == device::INVALID_DRIVER_ID) return KStatus::NotFound;
    if (target->driver >= g_count) return KStatus::Corrupted;

    Driver& driver = g_drivers[target->driver];
    if (driver.detach != nullptr) driver.detach(*target, driver.context);
    target = device::resolve_mutable(target_handle);
    if (target == nullptr || target->driver != driver.id) {
        if (driver.attached_count != 0U) --driver.attached_count;
        driver.status = driver.attached_count == 0U
            ? Status::Registered : Status::Ready;
        bump_lifecycle(driver);
        return KStatus::NoDevice;
    }
    const KStatus release_status = device::release(id, driver.id);
    if (release_status != KStatus::Ok) return release_status;
    if (driver.attached_count != 0U) --driver.attached_count;
    driver.status = driver.attached_count == 0U
        ? Status::Registered : Status::Ready;
    bump_lifecycle(driver);
    return KStatus::Ok;
}

KStatus rebind_device(device::DeviceId id) {
    if (!g_initialized || !device::initialized()) return KStatus::BadState;
    device::Device* target = device::get_mutable(id);
    if (target == nullptr) return KStatus::NotFound;
    if (target->driver != device::INVALID_DRIVER_ID) {
        const KStatus unbind_status = unbind_device(id);
        if (unbind_status != KStatus::Ok) return unbind_status;
    }
    return bind_device(id);
}

KStatus report_device_failure(device::DeviceId id, KStatus reason) {
    if (!g_initialized || !device::initialized()) return KStatus::BadState;
    if (reason == KStatus::Ok) return KStatus::InvalidArgument;
    const device::DeviceHandle target_handle = device::handle_for(id);
    device::Device* target = device::resolve_mutable(target_handle);
    if (target == nullptr) return KStatus::NotFound;

    if (target->driver == device::INVALID_DRIVER_ID) {
        static_cast<void>(device::set_status(id, device::Status::Failed));
        return KStatus::Ok;
    }
    if (target->driver >= g_count) return KStatus::Corrupted;

    Driver& driver = g_drivers[target->driver];
    record_failure(driver, id, FailureStage::Runtime, reason);
    if (driver.detach != nullptr) driver.detach(*target, driver.context);
    target = device::resolve_mutable(target_handle);
    if (target == nullptr || target->driver != driver.id) {
        if (driver.attached_count != 0U) --driver.attached_count;
        return KStatus::NoDevice;
    }
    const KStatus release_status = device::release(id, driver.id);
    if (release_status != KStatus::Ok) return release_status;
    if (driver.attached_count != 0U) --driver.attached_count;
    static_cast<void>(device::set_status(id, device::Status::Failed));
    return KStatus::Ok;
}

KStatus bind_all() {
    if (!g_initialized || !device::initialized()) {
        return KStatus::BadState;
    }
    KStatus aggregate = KStatus::Ok;
    for (device::DeviceId id = 0; id < device::count(); ++id) {
        const device::Device* target = device::get(id);
        if (target == nullptr || target->driver != device::INVALID_DRIVER_ID) {
            continue;
        }
        const KStatus status = bind_device(id);
        if (status != KStatus::Ok && status != KStatus::NotFound) {
            aggregate = status;
        }
    }
    return aggregate;
}

void visit(VisitCallback callback, void* context) {
    if (callback == nullptr) {
        return;
    }
    for (size_t index = 0; index < g_count; ++index) {
        if (!callback(g_drivers[index], context)) {
            break;
        }
    }
}

const char* status_name(Status status) {
    switch (status) {
        case Status::Registered: return "REGISTERED";
        case Status::Ready: return "READY";
        case Status::Degraded: return "DEGRADED";
        case Status::Failed: return "FAILED";
    }
    return "UNKNOWN";
}

} // namespace drivers::driver

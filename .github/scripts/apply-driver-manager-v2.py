from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: anchor count={count}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    'kernel/drivers/core/driver_manager.hpp',
    '''enum class Status : uint8_t {\n    Registered = 0,\n    Ready,\n    Degraded,\n    Failed,\n};\n''',
    '''enum class Status : uint8_t {\n    Registered = 0,\n    Ready,\n    Degraded,\n    Failed,\n};\n\nenum class FailureStage : uint8_t {\n    None = 0,\n    Probe,\n    Attach,\n    Runtime,\n};\n''',
)
replace_once(
    'kernel/drivers/core/driver_manager.hpp',
    '''    Status status;\n    size_t probe_count;\n    size_t attached_count;\n    size_t failure_count;\n};\n''',
    '''    Status status;\n    size_t probe_count;\n    size_t attached_count;\n    size_t failure_count;\n    KStatus last_failure;\n    device::DeviceId last_failure_device;\n    FailureStage last_failure_stage;\n    uint32_t lifecycle_generation;\n};\n''',
)
replace_once(
    'kernel/drivers/core/driver_manager.hpp',
    '''KStatus bind_device(device::DeviceId id);\nKStatus bind_all();\n''',
    '''KStatus bind_device(device::DeviceId id);\nKStatus unbind_device(device::DeviceId id);\nKStatus rebind_device(device::DeviceId id);\nKStatus report_device_failure(device::DeviceId id, KStatus reason);\nKStatus bind_all();\n''',
)
replace_once(
    'kernel/drivers/core/driver_manager.hpp',
    '''const char* status_name(Status status);\n''',
    '''const char* status_name(Status status);\nconst char* failure_stage_name(FailureStage stage);\n''',
)

source = Path('kernel/drivers/core/driver_manager.cpp')
text = source.read_text()
anchor = '''bool text_equal(const char* left, const char* right) {\n    if (left == nullptr || right == nullptr) {\n        return left == right;\n    }\n    while (*left != '\\0' && *left == *right) {\n        ++left;\n        ++right;\n    }\n    return *left == *right;\n}\n'''
if text.count(anchor) != 1:
    raise SystemExit('driver helper anchor mismatch')
helpers = r'''

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
'''
text = text.replace(anchor, anchor + helpers, 1)

replace_once(
    'kernel/drivers/core/driver_manager.cpp',
    '''    value.status = Status::Registered;\n    g_drivers[g_count] = value;\n''',
    '''    value.status = Status::Registered;\n    value.last_failure = KStatus::Ok;\n    value.last_failure_device = device::INVALID_DEVICE_ID;\n    value.last_failure_stage = FailureStage::None;\n    value.lifecycle_generation = 1U;\n    g_drivers[g_count] = value;\n''',
)

# Centralize failure accounting in bind_device.
text = text.replace(
    '''        if (status != KStatus::Ok) {\n            ++driver.failure_count;\n            driver.status = Status::Degraded;\n            last_failure = status;\n''',
    '''        if (status != KStatus::Ok) {\n            record_failure(driver, id, FailureStage::Probe, status);\n            last_failure = status;\n''',
    1,
)
text = text.replace(
    '''        if (status == KStatus::Ok) {\n            ++driver.attached_count;\n            driver.status = Status::Ready;\n            static_cast<void>(device::set_status(id, device::Status::Ready));\n            return KStatus::Ok;\n        }\n        ++driver.failure_count;\n        driver.status = Status::Degraded;\n        last_failure = status;\n''',
    '''        if (status == KStatus::Ok) {\n            ++driver.attached_count;\n            driver.status = Status::Ready;\n            driver.last_failure = KStatus::Ok;\n            driver.last_failure_device = device::INVALID_DEVICE_ID;\n            driver.last_failure_stage = FailureStage::None;\n            bump_lifecycle(driver);\n            static_cast<void>(device::set_status(id, device::Status::Ready));\n            return KStatus::Ok;\n        }\n        record_failure(driver, id, FailureStage::Attach, status);\n        last_failure = status;\n''',
    1,
)

insert_anchor = '''KStatus bind_all() {\n'''
if text.count(insert_anchor) != 1:
    raise SystemExit('driver bind_all insertion anchor mismatch')
functions = r'''KStatus unbind_device(device::DeviceId id) {
    if (!g_initialized || !device::initialized()) return KStatus::BadState;
    device::Device* target = device::get_mutable(id);
    if (target == nullptr) return KStatus::NotFound;
    if (target->driver == device::INVALID_DRIVER_ID) return KStatus::NotFound;
    if (target->driver >= g_count) return KStatus::Corrupted;

    Driver& driver = g_drivers[target->driver];
    if (driver.detach != nullptr) driver.detach(*target, driver.context);
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
    device::Device* target = device::get_mutable(id);
    if (target == nullptr) return KStatus::NotFound;

    if (target->driver == device::INVALID_DRIVER_ID) {
        static_cast<void>(device::set_status(id, device::Status::Failed));
        return KStatus::Ok;
    }
    if (target->driver >= g_count) return KStatus::Corrupted;

    Driver& driver = g_drivers[target->driver];
    record_failure(driver, id, FailureStage::Runtime, reason);
    if (driver.detach != nullptr) driver.detach(*target, driver.context);
    const KStatus release_status = device::release(id, driver.id);
    if (release_status != KStatus::Ok) return release_status;
    if (driver.attached_count != 0U) --driver.attached_count;
    static_cast<void>(device::set_status(id, device::Status::Failed));
    return KStatus::Ok;
}

'''
text = text.replace(insert_anchor, functions + insert_anchor, 1)

replace_once(
    'kernel/drivers/core/driver_manager.cpp',
    '''const char* status_name(Status status) {\n    switch (status) {\n        case Status::Registered: return "REGISTERED";\n        case Status::Ready: return "READY";\n        case Status::Degraded: return "DEGRADED";\n        case Status::Failed: return "FAILED";\n    }\n    return "UNKNOWN";\n}\n''',
    '''const char* status_name(Status status) {\n    switch (status) {\n        case Status::Registered: return "REGISTERED";\n        case Status::Ready: return "READY";\n        case Status::Degraded: return "DEGRADED";\n        case Status::Failed: return "FAILED";\n    }\n    return "UNKNOWN";\n}\n\nconst char* failure_stage_name(FailureStage stage) {\n    switch (stage) {\n        case FailureStage::None: return "NONE";\n        case FailureStage::Probe: return "PROBE";\n        case FailureStage::Attach: return "ATTACH";\n        case FailureStage::Runtime: return "RUNTIME";\n    }\n    return "UNKNOWN";\n}\n''',
)
source.write_text(text)

# One deferred backlog only.
todo = Path('TODO-DEFERRED-TESTS.md')
text = todo.read_text()
anchor = '## Filesystem foundation\n'
addition = (
    '- Driver Manager 2.0 regressions: probe/attach failure accounting, lifecycle generation, detach on unbind, claim release, attached-count accounting, rebind after failure, runtime failure isolation, invalid/corrupt binding rejection, failure-stage diagnostics.\n\n'
    + anchor
)
if text.count(anchor) != 1:
    raise SystemExit('Driver Manager v2 TODO anchor mismatch')
todo.write_text(text.replace(anchor, addition, 1))

print('Driver Manager 2.0 failure-isolation slice applied')

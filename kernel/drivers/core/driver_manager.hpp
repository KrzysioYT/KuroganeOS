#pragma once

#include <stddef.h>
#include <stdint.h>

#include "device_manager.hpp"

namespace drivers::driver {

constexpr size_t MAXIMUM_DRIVERS = 32;
constexpr size_t MAXIMUM_NAME_LENGTH = 31;

enum class Status : uint8_t {
    Registered = 0,
    Ready,
    Degraded,
    Failed,
};

enum class FailureStage : uint8_t {
    None = 0,
    Probe,
    Attach,
    Runtime,
};

using MatchCallback = bool (*)(const device::Device& device, void* context);
using LifecycleCallback = KStatus (*)(
    const device::Device& device,
    uint32_t timeout_ticks,
    void* context);
using DetachCallback = void (*)(const device::Device& device, void* context);

struct Descriptor {
    const char* name;
    int32_t priority;
    uint32_t timeout_ticks;
    MatchCallback match;
    LifecycleCallback probe;
    LifecycleCallback attach;
    DetachCallback detach;
    void* context;
};

struct Driver {
    device::DriverId id;
    char name[MAXIMUM_NAME_LENGTH + 1];
    int32_t priority;
    uint32_t timeout_ticks;
    MatchCallback match;
    LifecycleCallback probe;
    LifecycleCallback attach;
    DetachCallback detach;
    void* context;
    Status status;
    size_t probe_count;
    size_t attached_count;
    size_t failure_count;
    KStatus last_failure;
    device::DeviceId last_failure_device;
    FailureStage last_failure_stage;
    uint32_t lifecycle_generation;
};

using VisitCallback = bool (*)(const Driver& driver, void* context);

KStatus initialize();
bool initialized();
size_t count();
KStatus register_driver(const Descriptor& descriptor, device::DriverId* id);
const Driver* get(device::DriverId id);
const Driver* find(const char* name);
KStatus bind_device(device::DeviceId id);
KStatus unbind_device(device::DeviceId id);
KStatus rebind_device(device::DeviceId id);
KStatus report_device_failure(device::DeviceId id, KStatus reason);
KStatus bind_all();
void visit(VisitCallback callback, void* context);
const char* status_name(Status status);
const char* failure_stage_name(FailureStage stage);

} // namespace drivers::driver

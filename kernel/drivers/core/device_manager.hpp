#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../../libk/status.hpp"

namespace drivers::device {

constexpr size_t MAXIMUM_DEVICES = 192;
constexpr size_t MAXIMUM_RESOURCES = 8;
constexpr size_t MAXIMUM_CHILDREN = 16;
constexpr size_t MAXIMUM_NAME_LENGTH = 47;
constexpr size_t MAXIMUM_DRIVER_NAME_LENGTH = 31;

using DeviceId = uint32_t;
using DriverId = uint32_t;
using DeviceHandle = uint64_t;
constexpr DeviceId INVALID_DEVICE_ID = UINT32_MAX;
constexpr DriverId INVALID_DRIVER_ID = UINT32_MAX;
constexpr DeviceHandle INVALID_DEVICE_HANDLE = UINT64_C(0);

enum Capability : uint64_t {
    CapabilityNone = UINT64_C(0),
    CapabilityIoPort = UINT64_C(1) << 0,
    CapabilityMmio = UINT64_C(1) << 1,
    CapabilityInterrupt = UINT64_C(1) << 2,
    CapabilityDma = UINT64_C(1) << 3,
    CapabilityChildEnumeration = UINT64_C(1) << 4,
    CapabilityHotRemove = UINT64_C(1) << 5,
    CapabilityDriverBinding = UINT64_C(1) << 6,
};

enum class Type : uint8_t {
    Unknown = 0,
    Platform,
    Processor,
    Display,
    StorageController,
    Block,
    Input,
    Network,
    UsbController,
    Bridge,
};

enum class Bus : uint8_t {
    Platform = 0,
    Pci,
    Usb,
    Virtual,
};

enum class Status : uint8_t {
    Discovered = 0,
    Probing,
    Initializing,
    Ready,
    Degraded,
    Failed,
    Disabled,
};

enum class ResourceType : uint8_t {
    IoPort = 0,
    Mmio,
    Irq,
    Dma,
};

struct Resource {
    ResourceType type;
    uint64_t start;
    uint64_t length;
    uint32_t flags;
};

struct BusAddress {
    uint8_t segment;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
};

struct Descriptor {
    Type type;
    Bus bus;
    const char* name;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t programming_interface;
    BusAddress bus_address;
    DeviceId parent;
    const Resource* resources;
    size_t resource_count;
};

struct Device {
    DeviceId id;
    uint32_t generation;
    uint32_t lifecycle_generation;
    uint64_t capabilities;
    Type type;
    Bus bus;
    char name[MAXIMUM_NAME_LENGTH + 1];
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t programming_interface;
    BusAddress bus_address;
    Status status;
    DriverId driver;
    char driver_name[MAXIMUM_DRIVER_NAME_LENGTH + 1];
    Resource resources[MAXIMUM_RESOURCES];
    size_t resource_count;
    DeviceId parent;
    DeviceId children[MAXIMUM_CHILDREN];
    size_t child_count;
    bool active;
};

using VisitCallback = bool (*)(const Device& device, void* context);

KStatus initialize();
bool initialized();
// count() remains the legacy high-water slot count so existing ID iteration is stable.
size_t count();
size_t active_count();
KStatus register_device(const Descriptor& descriptor, DeviceId* id);
KStatus remove_device(DeviceId id);
const Device* get(DeviceId id);
Device* get_mutable(DeviceId id);
DeviceHandle handle_for(DeviceId id);
// Checked resolution distinguishes malformed handles from well-formed handles
// whose generation no longer names the active device in that slot.
KStatus resolve_checked(DeviceHandle handle, const Device** output);
KStatus resolve_mutable_checked(DeviceHandle handle, Device** output);
const Device* resolve(DeviceHandle handle);
Device* resolve_mutable(DeviceHandle handle);
KStatus get_resource(DeviceHandle handle, size_t index, Resource* output);
bool has_capability(DeviceHandle handle, uint64_t capability_mask);
const Device* find_pci(uint8_t bus, uint8_t slot, uint8_t function);
KStatus set_status(DeviceId id, Status status);
// Driver ownership transitions advance the public handle generation. Callers
// must enumerate again after bind/unbind instead of carrying a handle across
// a changed resource owner.
KStatus claim(DeviceId id, DriverId driver, const char* driver_name);
KStatus release(DeviceId id, DriverId driver);
void visit(VisitCallback callback, void* context);

const char* type_name(Type type);
const char* bus_name(Bus bus);
const char* status_name(Status status);

} // namespace drivers::device

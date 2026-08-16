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
constexpr DeviceId INVALID_DEVICE_ID = UINT32_MAX;
constexpr DriverId INVALID_DRIVER_ID = UINT32_MAX;

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
};

using VisitCallback = bool (*)(const Device& device, void* context);

KStatus initialize();
bool initialized();
size_t count();
KStatus register_device(const Descriptor& descriptor, DeviceId* id);
const Device* get(DeviceId id);
Device* get_mutable(DeviceId id);
const Device* find_pci(uint8_t bus, uint8_t slot, uint8_t function);
KStatus set_status(DeviceId id, Status status);
KStatus claim(DeviceId id, DriverId driver, const char* driver_name);
KStatus release(DeviceId id, DriverId driver);
void visit(VisitCallback callback, void* context);

const char* type_name(Type type);
const char* bus_name(Bus bus);
const char* status_name(Status status);

} // namespace drivers::device

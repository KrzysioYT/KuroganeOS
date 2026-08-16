#include "device_manager.hpp"

namespace drivers::device {
namespace {

Device g_devices[MAXIMUM_DEVICES]{};
size_t g_count = 0;
bool g_initialized = false;

size_t copy_text(char* destination, size_t capacity, const char* source) {
    if (destination == nullptr || capacity == 0) {
        return 0;
    }
    size_t length = 0;
    if (source != nullptr) {
        while (length + 1 < capacity && source[length] != '\0') {
            destination[length] = source[length];
            ++length;
        }
    }
    destination[length] = '\0';
    return length;
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

} // namespace

KStatus initialize() {
    for (size_t index = 0; index < MAXIMUM_DEVICES; ++index) {
        g_devices[index] = {};
    }
    g_count = 0;
    g_initialized = true;
    return KStatus::Ok;
}

bool initialized() { return g_initialized; }
size_t count() { return g_count; }

KStatus register_device(const Descriptor& descriptor, DeviceId* id) {
    if (!g_initialized) {
        return KStatus::BadState;
    }
    if (id == nullptr || descriptor.name == nullptr ||
        descriptor.name[0] == '\0' ||
        descriptor.resource_count > MAXIMUM_RESOURCES ||
        (descriptor.resource_count != 0 && descriptor.resources == nullptr)) {
        return KStatus::InvalidArgument;
    }
    *id = INVALID_DEVICE_ID;
    Device* parent = nullptr;
    if (descriptor.parent != INVALID_DEVICE_ID) {
        parent = get_mutable(descriptor.parent);
        if (parent == nullptr) {
            return KStatus::NotFound;
        }
        if (parent->child_count >= MAXIMUM_CHILDREN) {
            return KStatus::Busy;
        }
    }
    if (g_count >= MAXIMUM_DEVICES) {
        return KStatus::NoMemory;
    }
    if (descriptor.bus == Bus::Pci &&
        find_pci(
            descriptor.bus_address.bus,
            descriptor.bus_address.slot,
            descriptor.bus_address.function) != nullptr) {
        return KStatus::AlreadyExists;
    }

    Device value{};
    value.id = static_cast<DeviceId>(g_count);
    value.type = descriptor.type;
    value.bus = descriptor.bus;
    copy_text(value.name, sizeof(value.name), descriptor.name);
    value.vendor_id = descriptor.vendor_id;
    value.device_id = descriptor.device_id;
    value.class_code = descriptor.class_code;
    value.subclass = descriptor.subclass;
    value.programming_interface = descriptor.programming_interface;
    value.bus_address = descriptor.bus_address;
    value.status = Status::Discovered;
    value.driver = INVALID_DRIVER_ID;
    value.parent = descriptor.parent;
    value.resource_count = descriptor.resource_count;
    for (size_t index = 0; index < descriptor.resource_count; ++index) {
        value.resources[index] = descriptor.resources[index];
    }

    g_devices[g_count] = value;
    if (parent != nullptr) {
        parent->children[parent->child_count++] = value.id;
    }
    ++g_count;
    *id = value.id;
    return KStatus::Ok;
}

const Device* get(DeviceId id) {
    return id < g_count ? &g_devices[id] : nullptr;
}

Device* get_mutable(DeviceId id) {
    return id < g_count ? &g_devices[id] : nullptr;
}

const Device* find_pci(uint8_t bus, uint8_t slot, uint8_t function) {
    for (size_t index = 0; index < g_count; ++index) {
        const Device& device = g_devices[index];
        if (device.bus == Bus::Pci && device.bus_address.bus == bus &&
            device.bus_address.slot == slot &&
            device.bus_address.function == function) {
            return &device;
        }
    }
    return nullptr;
}

KStatus set_status(DeviceId id, Status status) {
    Device* device = get_mutable(id);
    if (device == nullptr) {
        return KStatus::NotFound;
    }
    device->status = status;
    return KStatus::Ok;
}

KStatus claim(DeviceId id, DriverId driver, const char* driver_name) {
    Device* device = get_mutable(id);
    if (device == nullptr) {
        return KStatus::NotFound;
    }
    if (driver == INVALID_DRIVER_ID || driver_name == nullptr ||
        driver_name[0] == '\0') {
        return KStatus::InvalidArgument;
    }
    if (device->driver != INVALID_DRIVER_ID) {
        return device->driver == driver &&
            text_equal(device->driver_name, driver_name)
            ? KStatus::AlreadyExists
            : KStatus::Busy;
    }
    device->driver = driver;
    copy_text(device->driver_name, sizeof(device->driver_name), driver_name);
    return KStatus::Ok;
}

KStatus release(DeviceId id, DriverId driver) {
    Device* device = get_mutable(id);
    if (device == nullptr) {
        return KStatus::NotFound;
    }
    if (device->driver != driver || driver == INVALID_DRIVER_ID) {
        return KStatus::PermissionDenied;
    }
    device->driver = INVALID_DRIVER_ID;
    device->driver_name[0] = '\0';
    device->status = Status::Discovered;
    return KStatus::Ok;
}

void visit(VisitCallback callback, void* context) {
    if (callback == nullptr) {
        return;
    }
    for (size_t index = 0; index < g_count; ++index) {
        if (!callback(g_devices[index], context)) {
            break;
        }
    }
}

const char* type_name(Type type) {
    switch (type) {
        case Type::Unknown: return "Unknown";
        case Type::Platform: return "Platform";
        case Type::Processor: return "CPU";
        case Type::Display: return "Display";
        case Type::StorageController: return "StorageController";
        case Type::Block: return "Block";
        case Type::Input: return "Input";
        case Type::Network: return "Network";
        case Type::UsbController: return "USBController";
        case Type::Bridge: return "Bridge";
    }
    return "Unknown";
}

const char* bus_name(Bus bus) {
    switch (bus) {
        case Bus::Platform: return "Platform";
        case Bus::Pci: return "PCI";
        case Bus::Usb: return "USB";
        case Bus::Virtual: return "Virtual";
    }
    return "Unknown";
}

const char* status_name(Status status) {
    switch (status) {
        case Status::Discovered: return "DISCOVERED";
        case Status::Probing: return "PROBING";
        case Status::Initializing: return "INITIALIZING";
        case Status::Ready: return "READY";
        case Status::Degraded: return "DEGRADED";
        case Status::Failed: return "FAILED";
        case Status::Disabled: return "DISABLED";
    }
    return "UNKNOWN";
}

} // namespace drivers::device

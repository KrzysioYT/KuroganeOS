#include "device_manager.hpp"

namespace drivers::device {
namespace {

Device g_devices[MAXIMUM_DEVICES]{};
size_t g_count = 0;
size_t g_active_count = 0;
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


uint64_t resource_capability(ResourceType type) {
    switch (type) {
        case ResourceType::IoPort: return CapabilityIoPort;
        case ResourceType::Mmio: return CapabilityMmio;
        case ResourceType::Irq: return CapabilityInterrupt;
        case ResourceType::Dma: return CapabilityDma;
    }
    return CapabilityNone;
}

uint64_t derive_capabilities(const Descriptor& descriptor) {
    uint64_t capabilities = CapabilityDriverBinding;
    for (size_t index = 0U; index < descriptor.resource_count; ++index) {
        capabilities |= resource_capability(descriptor.resources[index].type);
    }
    if (descriptor.type == Type::Platform || descriptor.type == Type::Bridge ||
        descriptor.type == Type::StorageController ||
        descriptor.type == Type::UsbController) {
        capabilities |= CapabilityChildEnumeration;
    }
    if (descriptor.bus == Bus::Usb || descriptor.bus == Bus::Virtual) {
        capabilities |= CapabilityHotRemove;
    }
    return capabilities;
}

DeviceHandle encode_handle(DeviceId id, uint32_t generation) {
    if (id == INVALID_DEVICE_ID || generation == 0U) return INVALID_DEVICE_HANDLE;
    return (static_cast<uint64_t>(generation) << 32U) |
        (static_cast<uint64_t>(id) + UINT64_C(1));
}

bool decode_handle(DeviceHandle handle, DeviceId* id, uint32_t* generation) {
    if (handle == INVALID_DEVICE_HANDLE || id == nullptr || generation == nullptr) {
        return false;
    }
    const uint64_t encoded_id = handle & UINT64_C(0xffffffff);
    const uint32_t encoded_generation = static_cast<uint32_t>(handle >> 32U);
    if (encoded_id == 0U || encoded_id > MAXIMUM_DEVICES ||
        encoded_generation == 0U) return false;
    *id = static_cast<DeviceId>(encoded_id - 1U);
    *generation = encoded_generation;
    return true;
}

void bump_lifecycle(Device& device) {
    ++device.lifecycle_generation;
    if (device.lifecycle_generation == 0U) device.lifecycle_generation = 1U;
}

void bump_handle_generation(Device& device) {
    ++device.generation;
    if (device.generation == 0U) device.generation = 1U;
}

void unlink_child(Device& parent, DeviceId child) {
    for (size_t index = 0U; index < parent.child_count; ++index) {
        if (parent.children[index] != child) continue;
        for (size_t move = index + 1U; move < parent.child_count; ++move) {
            parent.children[move - 1U] = parent.children[move];
        }
        --parent.child_count;
        if (parent.child_count < MAXIMUM_CHILDREN) {
            parent.children[parent.child_count] = INVALID_DEVICE_ID;
        }
        return;
    }
}

} // namespace

KStatus initialize() {
    for (size_t index = 0; index < MAXIMUM_DEVICES; ++index) {
        const uint32_t generation = g_devices[index].generation;
        g_devices[index] = {};
        g_devices[index].generation = generation;
        g_devices[index].id = static_cast<DeviceId>(index);
        g_devices[index].driver = INVALID_DRIVER_ID;
        g_devices[index].parent = INVALID_DEVICE_ID;
    }
    g_count = 0;
    g_active_count = 0;
    g_initialized = true;
    return KStatus::Ok;
}

bool initialized() { return g_initialized; }
size_t count() { return g_count; }
size_t active_count() { return g_active_count; }

KStatus register_device(const Descriptor& descriptor, DeviceId* id) {
    if (!g_initialized) return KStatus::BadState;
    if (id == nullptr || descriptor.name == nullptr || descriptor.name[0] == '\0' ||
        descriptor.resource_count > MAXIMUM_RESOURCES ||
        (descriptor.resource_count != 0U && descriptor.resources == nullptr)) {
        return KStatus::InvalidArgument;
    }
    *id = INVALID_DEVICE_ID;
    Device* parent = nullptr;
    if (descriptor.parent != INVALID_DEVICE_ID) {
        parent = get_mutable(descriptor.parent);
        if (parent == nullptr) return KStatus::NotFound;
        if (parent->child_count >= MAXIMUM_CHILDREN) return KStatus::Busy;
    }
    if (descriptor.bus == Bus::Pci &&
        find_pci(descriptor.bus_address.bus, descriptor.bus_address.slot,
            descriptor.bus_address.function) != nullptr) {
        return KStatus::AlreadyExists;
    }

    size_t slot = MAXIMUM_DEVICES;
    for (size_t index = 0U; index < g_count; ++index) {
        if (!g_devices[index].active) {
            slot = index;
            break;
        }
    }
    if (slot == MAXIMUM_DEVICES) {
        if (g_count >= MAXIMUM_DEVICES) return KStatus::NoMemory;
        slot = g_count++;
    }

    uint32_t generation = g_devices[slot].generation;
    ++generation;
    if (generation == 0U) generation = 1U;
    Device value{};
    value.id = static_cast<DeviceId>(slot);
    value.generation = generation;
    value.lifecycle_generation = 1U;
    value.capabilities = derive_capabilities(descriptor);
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
    for (size_t index = 0U; index < MAXIMUM_CHILDREN; ++index) {
        value.children[index] = INVALID_DEVICE_ID;
    }
    for (size_t index = 0U; index < descriptor.resource_count; ++index) {
        value.resources[index] = descriptor.resources[index];
    }
    value.active = true;
    g_devices[slot] = value;
    if (parent != nullptr) parent->children[parent->child_count++] = value.id;
    ++g_active_count;
    *id = value.id;
    return KStatus::Ok;
}

KStatus remove_device(DeviceId id) {
    Device* device = get_mutable(id);
    if (device == nullptr) return KStatus::NotFound;
    if (device->driver != INVALID_DRIVER_ID || device->child_count != 0U) {
        return KStatus::Busy;
    }
    if ((device->capabilities & CapabilityHotRemove) == 0U) {
        return KStatus::PermissionDenied;
    }
    if (device->parent != INVALID_DEVICE_ID) {
        Device* parent = get_mutable(device->parent);
        if (parent != nullptr) unlink_child(*parent, id);
    }
    const uint32_t generation = device->generation;
    *device = {};
    device->id = id;
    device->generation = generation;
    device->driver = INVALID_DRIVER_ID;
    device->parent = INVALID_DEVICE_ID;
    for (size_t index = 0U; index < MAXIMUM_CHILDREN; ++index) {
        device->children[index] = INVALID_DEVICE_ID;
    }
    if (g_active_count != 0U) --g_active_count;
    return KStatus::Ok;
}

const Device* get(DeviceId id) {
    return id < g_count && g_devices[id].active ? &g_devices[id] : nullptr;
}

Device* get_mutable(DeviceId id) {
    return id < g_count && g_devices[id].active ? &g_devices[id] : nullptr;
}

DeviceHandle handle_for(DeviceId id) {
    const Device* device = get(id);
    return device == nullptr ? INVALID_DEVICE_HANDLE
        : encode_handle(device->id, device->generation);
}

const Device* resolve(DeviceHandle handle) {
    DeviceId id = INVALID_DEVICE_ID;
    uint32_t generation = 0U;
    if (!decode_handle(handle, &id, &generation)) return nullptr;
    const Device* device = get(id);
    return device != nullptr && device->generation == generation ? device : nullptr;
}

Device* resolve_mutable(DeviceHandle handle) {
    DeviceId id = INVALID_DEVICE_ID;
    uint32_t generation = 0U;
    if (!decode_handle(handle, &id, &generation)) return nullptr;
    Device* device = get_mutable(id);
    return device != nullptr && device->generation == generation ? device : nullptr;
}

KStatus get_resource(DeviceHandle handle, size_t index, Resource* output) {
    if (output == nullptr) return KStatus::InvalidArgument;
    *output = {};
    const Device* device = resolve(handle);
    if (device == nullptr) return KStatus::NotFound;
    if (index >= device->resource_count) return KStatus::OutOfRange;
    *output = device->resources[index];
    return KStatus::Ok;
}

bool has_capability(DeviceHandle handle, uint64_t capability_mask) {
    if (capability_mask == CapabilityNone) return false;
    const Device* device = resolve(handle);
    return device != nullptr &&
        (device->capabilities & capability_mask) == capability_mask;
}

const Device* find_pci(uint8_t bus, uint8_t slot, uint8_t function) {
    for (size_t index = 0; index < g_count; ++index) {
        const Device& device = g_devices[index];
        if (!device.active) continue;
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
    if (device->status != status) {
        device->status = status;
        bump_lifecycle(*device);
    }
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
    bump_handle_generation(*device);
    bump_lifecycle(*device);
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
    bump_handle_generation(*device);
    bump_lifecycle(*device);
    return KStatus::Ok;
}

void visit(VisitCallback callback, void* context) {
    if (callback == nullptr) {
        return;
    }
    for (size_t index = 0; index < g_count; ++index) {
        if (!g_devices[index].active) continue;
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

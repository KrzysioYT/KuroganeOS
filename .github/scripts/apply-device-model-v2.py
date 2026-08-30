from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: anchor count={count}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    'kernel/drivers/core/device_manager.hpp',
    '''using DeviceId = uint32_t;\nusing DriverId = uint32_t;\nconstexpr DeviceId INVALID_DEVICE_ID = UINT32_MAX;\nconstexpr DriverId INVALID_DRIVER_ID = UINT32_MAX;\n''',
    '''using DeviceId = uint32_t;\nusing DriverId = uint32_t;\nusing DeviceHandle = uint64_t;\nconstexpr DeviceId INVALID_DEVICE_ID = UINT32_MAX;\nconstexpr DriverId INVALID_DRIVER_ID = UINT32_MAX;\nconstexpr DeviceHandle INVALID_DEVICE_HANDLE = UINT64_C(0);\n\nenum Capability : uint64_t {\n    CapabilityNone = UINT64_C(0),\n    CapabilityIoPort = UINT64_C(1) << 0,\n    CapabilityMmio = UINT64_C(1) << 1,\n    CapabilityInterrupt = UINT64_C(1) << 2,\n    CapabilityDma = UINT64_C(1) << 3,\n    CapabilityChildEnumeration = UINT64_C(1) << 4,\n    CapabilityHotRemove = UINT64_C(1) << 5,\n    CapabilityDriverBinding = UINT64_C(1) << 6,\n};\n''',
)

replace_once(
    'kernel/drivers/core/device_manager.hpp',
    '''struct Device {\n    DeviceId id;\n    Type type;\n''',
    '''struct Device {\n    DeviceId id;\n    uint32_t generation;\n    uint32_t lifecycle_generation;\n    uint64_t capabilities;\n    Type type;\n''',
)
replace_once(
    'kernel/drivers/core/device_manager.hpp',
    '''    DeviceId children[MAXIMUM_CHILDREN];\n    size_t child_count;\n};\n''',
    '''    DeviceId children[MAXIMUM_CHILDREN];\n    size_t child_count;\n    bool active;\n};\n''',
)
replace_once(
    'kernel/drivers/core/device_manager.hpp',
    '''bool initialized();\nsize_t count();\nKStatus register_device(const Descriptor& descriptor, DeviceId* id);\nconst Device* get(DeviceId id);\nDevice* get_mutable(DeviceId id);\n''',
    '''bool initialized();\n// count() remains the legacy high-water slot count so existing ID iteration is stable.\nsize_t count();\nsize_t active_count();\nKStatus register_device(const Descriptor& descriptor, DeviceId* id);\nKStatus remove_device(DeviceId id);\nconst Device* get(DeviceId id);\nDevice* get_mutable(DeviceId id);\nDeviceHandle handle_for(DeviceId id);\nconst Device* resolve(DeviceHandle handle);\nDevice* resolve_mutable(DeviceHandle handle);\nKStatus get_resource(DeviceHandle handle, size_t index, Resource* output);\nbool has_capability(DeviceHandle handle, uint64_t capability_mask);\n''',
)

source = Path('kernel/drivers/core/device_manager.cpp')
text = source.read_text()
text = text.replace(
    '''Device g_devices[MAXIMUM_DEVICES]{};\nsize_t g_count = 0;\nbool g_initialized = false;\n''',
    '''Device g_devices[MAXIMUM_DEVICES]{};\nsize_t g_count = 0;\nsize_t g_active_count = 0;\nbool g_initialized = false;\n''',
    1,
)
helper_anchor = '''bool text_equal(const char* left, const char* right) {\n    if (left == nullptr || right == nullptr) {\n        return left == right;\n    }\n    while (*left != '\\0' && *left == *right) {\n        ++left;\n        ++right;\n    }\n    return *left == *right;\n}\n'''
if text.count(helper_anchor) != 1:
    raise SystemExit('device helper anchor mismatch')
helpers = r'''

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
'''
text = text.replace(helper_anchor, helper_anchor + helpers, 1)

text = text.replace(
    '''    g_count = 0;\n    g_initialized = true;\n''',
    '''    g_count = 0;\n    g_active_count = 0;\n    g_initialized = true;\n''',
    1,
)
text = text.replace(
    '''bool initialized() { return g_initialized; }\nsize_t count() { return g_count; }\n''',
    '''bool initialized() { return g_initialized; }\nsize_t count() { return g_count; }\nsize_t active_count() { return g_active_count; }\n''',
    1,
)

start = text.find('KStatus register_device(const Descriptor& descriptor, DeviceId* id) {\n')
end = text.find('\nconst Device* get(DeviceId id) {', start)
if start < 0 or end < 0:
    raise SystemExit('register_device boundaries missing')
register_impl = r'''KStatus register_device(const Descriptor& descriptor, DeviceId* id) {
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
'''
text = text[:start] + register_impl + text[end:]

text = text.replace(
    '''const Device* get(DeviceId id) {\n    return id < g_count ? &g_devices[id] : nullptr;\n}\n\nDevice* get_mutable(DeviceId id) {\n    return id < g_count ? &g_devices[id] : nullptr;\n}\n''',
    '''const Device* get(DeviceId id) {\n    return id < g_count && g_devices[id].active ? &g_devices[id] : nullptr;\n}\n\nDevice* get_mutable(DeviceId id) {\n    return id < g_count && g_devices[id].active ? &g_devices[id] : nullptr;\n}\n\nDeviceHandle handle_for(DeviceId id) {\n    const Device* device = get(id);\n    return device == nullptr ? INVALID_DEVICE_HANDLE\n        : encode_handle(device->id, device->generation);\n}\n\nconst Device* resolve(DeviceHandle handle) {\n    DeviceId id = INVALID_DEVICE_ID;\n    uint32_t generation = 0U;\n    if (!decode_handle(handle, &id, &generation)) return nullptr;\n    const Device* device = get(id);\n    return device != nullptr && device->generation == generation ? device : nullptr;\n}\n\nDevice* resolve_mutable(DeviceHandle handle) {\n    DeviceId id = INVALID_DEVICE_ID;\n    uint32_t generation = 0U;\n    if (!decode_handle(handle, &id, &generation)) return nullptr;\n    Device* device = get_mutable(id);\n    return device != nullptr && device->generation == generation ? device : nullptr;\n}\n\nKStatus get_resource(DeviceHandle handle, size_t index, Resource* output) {\n    if (output == nullptr) return KStatus::InvalidArgument;\n    *output = {};\n    const Device* device = resolve(handle);\n    if (device == nullptr) return KStatus::NotFound;\n    if (index >= device->resource_count) return KStatus::OutOfRange;\n    *output = device->resources[index];\n    return KStatus::Ok;\n}\n\nbool has_capability(DeviceHandle handle, uint64_t capability_mask) {\n    if (capability_mask == CapabilityNone) return false;\n    const Device* device = resolve(handle);\n    return device != nullptr &&\n        (device->capabilities & capability_mask) == capability_mask;\n}\n''',
    1,
)

text = text.replace(
    '''    for (size_t index = 0; index < g_count; ++index) {\n        const Device& device = g_devices[index];\n        if (device.bus == Bus::Pci''',
    '''    for (size_t index = 0; index < g_count; ++index) {\n        const Device& device = g_devices[index];\n        if (!device.active) continue;\n        if (device.bus == Bus::Pci''',
    1,
)
text = text.replace(
    '''    device->status = status;\n    return KStatus::Ok;\n}\n\nKStatus claim''',
    '''    if (device->status != status) {\n        device->status = status;\n        bump_lifecycle(*device);\n    }\n    return KStatus::Ok;\n}\n\nKStatus claim''',
    1,
)
text = text.replace(
    '''    device->driver = driver;\n    copy_text(device->driver_name, sizeof(device->driver_name), driver_name);\n    return KStatus::Ok;\n''',
    '''    device->driver = driver;\n    copy_text(device->driver_name, sizeof(device->driver_name), driver_name);\n    bump_lifecycle(*device);\n    return KStatus::Ok;\n''',
    1,
)
text = text.replace(
    '''    device->status = Status::Discovered;\n    return KStatus::Ok;\n}\n\nvoid visit''',
    '''    device->status = Status::Discovered;\n    bump_lifecycle(*device);\n    return KStatus::Ok;\n}\n\nvoid visit''',
    1,
)
text = text.replace(
    '''    for (size_t index = 0; index < g_count; ++index) {\n        if (!callback(g_devices[index], context)) {\n''',
    '''    for (size_t index = 0; index < g_count; ++index) {\n        if (!g_devices[index].active) continue;\n        if (!callback(g_devices[index], context)) {\n''',
    1,
)
source.write_text(text)

# Keep deferred verification in the single owner-requested backlog.
todo = Path('TODO-DEFERRED-TESTS.md')
text = todo.read_text()
anchor = '## 4.0 KuroFS deferred tests\n'
replacement = (
    '## 4.0 Pre-Steel / Device Model 2.0\n'
    '- Device Model 2.0 regressions: generation-safe handle stale rejection, slot reuse, active/high-water accounting, hot-remove policy, parent unlinking, child/claimed removal rejection, capability derivation, resource query bounds, lifecycle generation on state/claim/release.\n\n'
    '## Filesystem foundation\n'
    '- KuroFS v1 metadata core: format/mount on bounded block devices, redundant superblock fallback/disagreement, CRC corruption, geometry overflow/rejection, root inode validation, bitmap metadata reservation, flush/write failure propagation.\n'
)
if text.count(anchor) != 1:
    raise SystemExit('Pre-Steel TODO anchor mismatch')
# Existing KuroFS bullet immediately follows the heading; replace heading only and
# let a second replacement normalize the duplicated bullet section.
text = text.replace(anchor, replacement, 1)
duplicate = '- KuroFS v1 metadata core: format/mount on bounded block devices, redundant superblock fallback/disagreement, CRC corruption, geometry overflow/rejection, root inode validation, bitmap metadata reservation, flush/write failure propagation.\n\n- KuroFS v1 metadata core: format/mount on bounded block devices, redundant superblock fallback/disagreement, CRC corruption, geometry overflow/rejection, root inode validation, bitmap metadata reservation, flush/write failure propagation.\n'
if duplicate in text:
    text = text.replace(duplicate, '- KuroFS v1 metadata core: format/mount on bounded block devices, redundant superblock fallback/disagreement, CRC corruption, geometry overflow/rejection, root inode validation, bitmap metadata reservation, flush/write failure propagation.\n', 1)
todo.write_text(text)

print('Device Model 2.0 handle/capability foundation applied')

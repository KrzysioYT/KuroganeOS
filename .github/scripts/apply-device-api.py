from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: anchor count={count}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


header = r'''#ifndef KUROGANE_SDK_DEVICE_H
#define KUROGANE_SDK_DEVICE_H

#include <stddef.h>
#include <stdint.h>

#include <kurogane/status.h>
#include <kurogane/syscall.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KU_DEVICE_INFO_VERSION UINT32_C(1)
#define KU_DEVICE_NAME_CAPACITY 48U
#define KU_DEVICE_DRIVER_NAME_CAPACITY 32U
#define KU_DEVICE_INVALID_HANDLE UINT64_C(0)

typedef uint64_t ku_device_handle_t;

enum ku_device_type {
    KU_DEVICE_UNKNOWN = 0,
    KU_DEVICE_PLATFORM = 1,
    KU_DEVICE_PROCESSOR = 2,
    KU_DEVICE_DISPLAY = 3,
    KU_DEVICE_STORAGE_CONTROLLER = 4,
    KU_DEVICE_BLOCK = 5,
    KU_DEVICE_INPUT = 6,
    KU_DEVICE_NETWORK = 7,
    KU_DEVICE_USB_CONTROLLER = 8,
    KU_DEVICE_BRIDGE = 9
};

enum ku_device_bus {
    KU_DEVICE_BUS_PLATFORM = 0,
    KU_DEVICE_BUS_PCI = 1,
    KU_DEVICE_BUS_USB = 2,
    KU_DEVICE_BUS_VIRTUAL = 3
};

enum ku_device_state {
    KU_DEVICE_DISCOVERED = 0,
    KU_DEVICE_PROBING = 1,
    KU_DEVICE_INITIALIZING = 2,
    KU_DEVICE_READY = 3,
    KU_DEVICE_DEGRADED = 4,
    KU_DEVICE_FAILED = 5,
    KU_DEVICE_DISABLED = 6
};

enum ku_device_resource_type {
    KU_DEVICE_RESOURCE_IO_PORT = 0,
    KU_DEVICE_RESOURCE_MMIO = 1,
    KU_DEVICE_RESOURCE_IRQ = 2,
    KU_DEVICE_RESOURCE_DMA = 3
};

enum ku_device_capability {
    KU_DEVICE_CAP_IO_PORT = UINT64_C(1) << 0,
    KU_DEVICE_CAP_MMIO = UINT64_C(1) << 1,
    KU_DEVICE_CAP_INTERRUPT = UINT64_C(1) << 2,
    KU_DEVICE_CAP_DMA = UINT64_C(1) << 3,
    KU_DEVICE_CAP_CHILD_ENUMERATION = UINT64_C(1) << 4,
    KU_DEVICE_CAP_HOT_REMOVE = UINT64_C(1) << 5,
    KU_DEVICE_CAP_DRIVER_BINDING = UINT64_C(1) << 6
};

typedef struct ku_device_info {
    uint32_t structure_size;
    uint32_t version;
    ku_device_handle_t handle;
    ku_device_handle_t parent;
    uint64_t capabilities;
    uint32_t lifecycle_generation;
    uint32_t type;
    uint32_t bus;
    uint32_t state;
    uint32_t resource_count;
    uint32_t child_count;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t programming_interface;
    uint8_t pci_segment;
    uint8_t pci_bus;
    uint8_t pci_slot;
    uint8_t pci_function;
    uint8_t reserved0;
    char name[KU_DEVICE_NAME_CAPACITY];
    char driver[KU_DEVICE_DRIVER_NAME_CAPACITY];
} ku_device_info;

typedef struct ku_device_resource {
    uint32_t structure_size;
    uint32_t type;
    uint64_t start;
    uint64_t length;
    uint32_t flags;
    uint32_t reserved;
} ku_device_resource;

static inline ku_status_t ku_device_enumerate(
    size_t active_index,
    ku_device_handle_t* handle) {
    if (handle == NULL) return KU_STATUS_INVALID_ARGUMENT;
    *handle = KU_DEVICE_INVALID_HANDLE;
    return (ku_status_t)ku_syscall3(
        KU_SYS_DEVICE_ENUMERATE,
        (uint64_t)active_index,
        (uint64_t)(uintptr_t)handle,
        0U);
}

static inline ku_status_t ku_device_query(
    ku_device_handle_t handle,
    ku_device_info* info) {
    if (handle == KU_DEVICE_INVALID_HANDLE || info == NULL) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    info->structure_size = sizeof(*info);
    info->version = KU_DEVICE_INFO_VERSION;
    return (ku_status_t)ku_syscall3(
        KU_SYS_DEVICE_QUERY,
        handle,
        (uint64_t)(uintptr_t)info,
        sizeof(*info));
}

static inline ku_status_t ku_device_get_resource(
    ku_device_handle_t handle,
    size_t index,
    ku_device_resource* resource) {
    if (handle == KU_DEVICE_INVALID_HANDLE || resource == NULL) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    resource->structure_size = sizeof(*resource);
    resource->reserved = 0U;
    return (ku_status_t)ku_syscall3(
        KU_SYS_DEVICE_RESOURCE,
        handle,
        (uint64_t)index,
        (uint64_t)(uintptr_t)resource);
}

#ifdef __cplusplus
}
#endif

#endif
'''
Path('sdk/include/kurogane/device.h').write_text(header)

replace_once(
    'sdk/include/kurogane/syscall.h',
    '''    KU_SYS_SOCKET_CLOSE = 62,\n    KU_SYS_SOCKET_POLL = 63\n};''',
    '''    KU_SYS_SOCKET_CLOSE = 62,\n    KU_SYS_SOCKET_POLL = 63,\n    KU_SYS_DEVICE_ENUMERATE = 64,\n    KU_SYS_DEVICE_QUERY = 65,\n    KU_SYS_DEVICE_RESOURCE = 66\n};''',
)

replace_once(
    'kernel/user/runtime.cpp',
    '#include <kurogane/desktop.h>\n',
    '#include <kurogane/desktop.h>\n#include <kurogane/device.h>\n',
)
replace_once(
    'kernel/user/runtime.cpp',
    '#include "../drivers/audio/ac97.hpp"\n',
    '#include "../drivers/audio/ac97.hpp"\n#include "../drivers/core/device_manager.hpp"\n',
)
replace_once(
    'kernel/user/runtime.cpp',
    '''        (number >= KU_SYS_EVENT_CREATE && number <= KU_SYS_EVENT_CLOSE) ||\n        (number >= KU_SYS_SOCKET_CREATE && number <= KU_SYS_SOCKET_POLL);''',
    '''        (number >= KU_SYS_EVENT_CREATE && number <= KU_SYS_EVENT_CLOSE) ||\n        (number >= KU_SYS_SOCKET_CREATE && number <= KU_SYS_SOCKET_POLL) ||\n        (number >= KU_SYS_DEVICE_ENUMERATE && number <= KU_SYS_DEVICE_RESOURCE);''',
)

runtime = Path('kernel/user/runtime.cpp')
text = runtime.read_text()
anchor = '''        case KU_SYS_SOCKET_CREATE: {\n'''
if text.count(anchor) != 1:
    raise SystemExit('device syscall insertion anchor mismatch')
handlers = r'''        case KU_SYS_DEVICE_ENUMERATE: {
            if (frame.rdx != 0U || frame.rdi > SIZE_MAX ||
                !validate_user_buffer(*context, frame.rsi, sizeof(ku_device_handle_t), true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            auto* output = reinterpret_cast<ku_device_handle_t*>(
                static_cast<uintptr_t>(frame.rsi));
            *output = KU_DEVICE_INVALID_HANDLE;
            const size_t requested = static_cast<size_t>(frame.rdi);
            size_t active_index = 0U;
            for (drivers::device::DeviceId id = 0U;
                 id < drivers::device::count(); ++id) {
                const drivers::device::Device* device = drivers::device::get(id);
                if (device == nullptr) continue;
                if (active_index == requested) {
                    *output = drivers::device::handle_for(id);
                    frame.rax = *output != KU_DEVICE_INVALID_HANDLE
                        ? static_cast<uint64_t>(KU_STATUS_OK)
                        : static_cast<uint64_t>(KU_STATUS_BAD_STATE);
                    return;
                }
                ++active_index;
            }
            frame.rax = static_cast<uint64_t>(KU_STATUS_END_OF_STREAM);
            return;
        }
        case KU_SYS_DEVICE_QUERY: {
            if (frame.rdi == KU_DEVICE_INVALID_HANDLE ||
                frame.rdx != sizeof(ku_device_info) ||
                !validate_user_buffer(*context, frame.rsi, sizeof(ku_device_info), true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            auto* output = reinterpret_cast<ku_device_info*>(
                static_cast<uintptr_t>(frame.rsi));
            if (output->structure_size != sizeof(*output) ||
                output->version != KU_DEVICE_INFO_VERSION) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_VERSION_MISMATCH);
                return;
            }
            const auto handle = static_cast<drivers::device::DeviceHandle>(frame.rdi);
            const drivers::device::Device* device = drivers::device::resolve(handle);
            if (device == nullptr) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_NOT_FOUND);
                return;
            }
            ku_device_info result{};
            result.structure_size = sizeof(result);
            result.version = KU_DEVICE_INFO_VERSION;
            result.handle = handle;
            result.parent = device->parent == drivers::device::INVALID_DEVICE_ID
                ? KU_DEVICE_INVALID_HANDLE
                : drivers::device::handle_for(device->parent);
            result.capabilities = device->capabilities;
            result.lifecycle_generation = device->lifecycle_generation;
            result.type = static_cast<uint32_t>(device->type);
            result.bus = static_cast<uint32_t>(device->bus);
            result.state = static_cast<uint32_t>(device->status);
            result.resource_count = static_cast<uint32_t>(device->resource_count);
            result.child_count = static_cast<uint32_t>(device->child_count);
            result.vendor_id = device->vendor_id;
            result.device_id = device->device_id;
            result.class_code = device->class_code;
            result.subclass = device->subclass;
            result.programming_interface = device->programming_interface;
            result.pci_segment = device->bus_address.segment;
            result.pci_bus = device->bus_address.bus;
            result.pci_slot = device->bus_address.slot;
            result.pci_function = device->bus_address.function;
            for (size_t index = 0U; index < KU_DEVICE_NAME_CAPACITY; ++index) {
                result.name[index] = device->name[index];
                if (device->name[index] == '\0') break;
            }
            for (size_t index = 0U; index < KU_DEVICE_DRIVER_NAME_CAPACITY; ++index) {
                result.driver[index] = device->driver_name[index];
                if (device->driver_name[index] == '\0') break;
            }
            *output = result;
            frame.rax = static_cast<uint64_t>(KU_STATUS_OK);
            return;
        }
        case KU_SYS_DEVICE_RESOURCE: {
            if (frame.rdi == KU_DEVICE_INVALID_HANDLE || frame.rsi > SIZE_MAX ||
                !validate_user_buffer(
                    *context, frame.rdx, sizeof(ku_device_resource), true)) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            auto* output = reinterpret_cast<ku_device_resource*>(
                static_cast<uintptr_t>(frame.rdx));
            if (output->structure_size != sizeof(*output) || output->reserved != 0U) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_INVALID_ARGUMENT);
                return;
            }
            drivers::device::Resource resource{};
            const KStatus status = drivers::device::get_resource(
                static_cast<drivers::device::DeviceHandle>(frame.rdi),
                static_cast<size_t>(frame.rsi),
                &resource);
            if (status == KStatus::NotFound) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_NOT_FOUND);
                return;
            }
            if (status == KStatus::OutOfRange) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_OUT_OF_RANGE);
                return;
            }
            if (status != KStatus::Ok) {
                frame.rax = static_cast<uint64_t>(KU_STATUS_BAD_STATE);
                return;
            }
            output->type = static_cast<uint32_t>(resource.type);
            output->start = resource.start;
            output->length = resource.length;
            output->flags = resource.flags;
            frame.rax = static_cast<uint64_t>(KU_STATUS_OK);
            return;
        }
'''
text = text.replace(anchor, handlers + anchor, 1)
runtime.write_text(text)

# Record verification for the later consolidated pass only.
todo = Path('TODO-DEFERRED-TESTS.md')
text = todo.read_text()
anchor = '## Filesystem foundation\n'
addition = (
    '- Ring-3 Device API regressions: active-index enumeration, stale generation handle query, ABI version/size validation, parent handle, capability/state/lifecycle snapshots, resource bounds, no writable MMIO/PIO/DMA mapping side effects.\n\n'
    + anchor
)
if text.count(anchor) != 1:
    raise SystemExit('Device API TODO insertion anchor mismatch')
todo.write_text(text.replace(anchor, addition, 1))

print('Ring-3 read-only Device API applied')

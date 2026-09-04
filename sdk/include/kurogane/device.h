#ifndef KUROGANE_SDK_DEVICE_H
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
#define KU_DEVICE_MAX_DEVICES 192U
#define KU_DEVICE_MAX_RESOURCES 8U
#define KU_DEVICE_MAX_CHILDREN 16U

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

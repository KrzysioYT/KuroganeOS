#include <kurogane/device.h>

#include "../../runtime/user.h"

static ku_device_handle_t g_handles[KU_DEVICE_MAX_DEVICES];

__attribute__((noreturn)) static void fail(uint64_t code) {
    (void)u_puts("[TEST] device_ring3_runtime: FAIL code=");
    (void)u_put_u64(code);
    (void)u_puts("\n");
    ku_exit((int32_t)code);
}

static uint64_t required_resource_capability(uint32_t type) {
    switch (type) {
        case KU_DEVICE_RESOURCE_IO_PORT: return KU_DEVICE_CAP_IO_PORT;
        case KU_DEVICE_RESOURCE_MMIO: return KU_DEVICE_CAP_MMIO;
        case KU_DEVICE_RESOURCE_IRQ: return KU_DEVICE_CAP_INTERRUPT;
        case KU_DEVICE_RESOURCE_DMA: return KU_DEVICE_CAP_DMA;
        default: return UINT64_C(0);
    }
}

__attribute__((noreturn)) void _start(void) {
    size_t count = 0U;
    int saw_parent = 0;
    int saw_resource = 0;
    int saw_bound_device = 0;
    int saw_pci_device = 0;

    for (;;) {
        ku_device_handle_t handle = KU_DEVICE_INVALID_HANDLE;
        const ku_status_t enumerate_status = ku_device_enumerate(count, &handle);
        if (enumerate_status == KU_STATUS_END_OF_STREAM) break;
        if (enumerate_status != KU_STATUS_OK ||
            handle == KU_DEVICE_INVALID_HANDLE ||
            count >= KU_DEVICE_MAX_DEVICES) {
            fail(10U);
        }
        for (size_t previous = 0U; previous < count; ++previous) {
            if (g_handles[previous] == handle) fail(11U);
        }
        g_handles[count] = handle;

        ku_device_info info = {0};
        if (ku_device_query(handle, &info) != KU_STATUS_OK ||
            info.structure_size != sizeof(info) ||
            info.version != KU_DEVICE_INFO_VERSION ||
            info.handle != handle || info.lifecycle_generation == 0U ||
            info.type > KU_DEVICE_BRIDGE ||
            info.bus > KU_DEVICE_BUS_VIRTUAL ||
            info.state > KU_DEVICE_DISABLED ||
            info.resource_count > KU_DEVICE_MAX_RESOURCES ||
            info.child_count > KU_DEVICE_MAX_CHILDREN ||
            info.name[KU_DEVICE_NAME_CAPACITY - 1U] != '\0' ||
            info.driver[KU_DEVICE_DRIVER_NAME_CAPACITY - 1U] != '\0' ||
            (info.capabilities & KU_DEVICE_CAP_DRIVER_BINDING) == 0U) {
            fail(12U);
        }
        if (info.bus == KU_DEVICE_BUS_PCI) saw_pci_device = 1;
        if (info.state == KU_DEVICE_READY && info.driver[0] != '\0') {
            saw_bound_device = 1;
        }
        if (info.parent != KU_DEVICE_INVALID_HANDLE) {
            ku_device_info parent = {0};
            if (ku_device_query(info.parent, &parent) != KU_STATUS_OK ||
                parent.child_count == 0U) {
                fail(13U);
            }
            saw_parent = 1;
        }

        for (size_t resource_index = 0U;
             resource_index < info.resource_count;
             ++resource_index) {
            ku_device_resource resource = {0};
            if (ku_device_get_resource(handle, resource_index, &resource) !=
                    KU_STATUS_OK ||
                resource.structure_size != sizeof(resource) ||
                resource.reserved != 0U || resource.length == 0U) {
                fail(14U);
            }
            const uint64_t required = required_resource_capability(resource.type);
            if (required == 0U || (info.capabilities & required) == 0U) {
                fail(15U);
            }
            saw_resource = 1;
        }
        ku_device_resource beyond = {0};
        if (ku_device_get_resource(handle, info.resource_count, &beyond) !=
            KU_STATUS_OUT_OF_RANGE) {
            fail(16U);
        }
        ++count;
    }

    if (count == 0U || !saw_parent || !saw_resource ||
        !saw_bound_device || !saw_pci_device) {
        fail(17U);
    }
    (void)u_puts("[TEST] device_discovery_ring3: PASS\n");
    (void)u_puts("[TEST] device_query_ring3: PASS\n");
    (void)u_puts("[TEST] device_resource_boundary_ring3: PASS\n");

    for (size_t index = 0U; index < count; ++index) {
        ku_device_info current = {0};
        if (ku_device_query(g_handles[index], &current) != KU_STATUS_OK) {
            fail(18U);
        }
    }
    const uint64_t encoded_id = g_handles[0] & UINT64_C(0xFFFFFFFF);
    const uint32_t generation = (uint32_t)(g_handles[0] >> 32U);
    uint32_t stale_generation = generation + 1U;
    if (stale_generation == 0U) stale_generation = generation - 1U;
    const ku_device_handle_t stale_handle =
        ((uint64_t)stale_generation << 32U) | encoded_id;
    ku_device_info stale = {0};
    if (stale_generation == 0U || stale_handle == g_handles[0] ||
        ku_device_query(stale_handle, &stale) != KU_STATUS_NOT_FOUND ||
        ku_device_query(KU_DEVICE_INVALID_HANDLE, &stale) !=
            KU_STATUS_INVALID_ARGUMENT) {
        fail(19U);
    }
    (void)u_puts("[TEST] device_stale_handle_ring3: PASS\n");
    (void)u_puts("[TEST] device_ring3_runtime: PASS\n");
    ku_exit(0);
}

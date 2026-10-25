#ifndef KUROGANE_SDK_SERVICE_H
#define KUROGANE_SDK_SERVICE_H

#include <kurogane/ipc.h>

/*
 * KuroganeOS 3.4 named-service contract.
 *
 * The kernel IPC endpoint table remains the single source of truth
 * for names, ownership, lifecycle and generation-safe handles. A
 * service registration adds bounded version/capability metadata to
 * that same endpoint; connect performs compatibility negotiation
 * before a channel is created.
 */
#define KU_SERVICE_NAME_CAPACITY KU_IPC_SERVICE_NAME_CAPACITY
#define KU_SERVICE_MESSAGE_CAPACITY KU_IPC_MESSAGE_CAPACITY
#define KU_SERVICE_DESCRIPTOR_ABI_VERSION UINT32_C(1)
#define KU_SERVICE_NEGOTIATION_ABI_VERSION UINT32_C(1)
#define KU_SERVICE_INFO_ABI_VERSION UINT32_C(1)
#define KU_SERVICE_DEFAULT_VERSION UINT32_C(1)

typedef ku_ipc_handle_t ku_service_endpoint_t;
typedef ku_ipc_handle_t ku_service_connection_t;
typedef ku_ipc_message ku_service_message;

typedef struct ku_service_descriptor {
    uint32_t structure_size;
    uint32_t abi_version;
    uint32_t service_version;
    uint32_t minimum_client_version;
    uint64_t capabilities;
    uint64_t reserved;
} ku_service_descriptor;

typedef struct ku_service_negotiation {
    uint32_t structure_size;
    uint32_t abi_version;
    uint32_t minimum_version;
    uint32_t maximum_version;
    uint32_t selected_version;
    uint32_t service_version;
    uint32_t minimum_client_version;
    uint32_t reserved;
    uint64_t capabilities;
    uint64_t owner_pid;
} ku_service_negotiation;

typedef struct ku_service_info {
    uint32_t structure_size;
    uint32_t abi_version;
    uint32_t service_version;
    uint32_t minimum_client_version;
    uint64_t capabilities;
    uint64_t owner_pid;
} ku_service_info;

#if defined(__cplusplus)
static_assert(sizeof(ku_service_descriptor) == 32, "service descriptor ABI mismatch");
static_assert(sizeof(ku_service_negotiation) == 48, "service negotiation ABI mismatch");
static_assert(sizeof(ku_service_info) == 32, "service info ABI mismatch");
#else
_Static_assert(sizeof(ku_service_descriptor) == 32, "service descriptor ABI mismatch");
_Static_assert(sizeof(ku_service_negotiation) == 48, "service negotiation ABI mismatch");
_Static_assert(sizeof(ku_service_info) == 32, "service info ABI mismatch");
#endif

static inline ku_result_t ku_service_register_versioned(
    const char* name,
    size_t name_size,
    const ku_service_descriptor* descriptor) {
    if (descriptor == NULL) return KU_STATUS_INVALID_ARGUMENT;
    return ku_syscall3(
        KU_SYS_IPC_BIND,
        (uint64_t)(uintptr_t)name,
        (uint64_t)name_size,
        (uint64_t)(uintptr_t)descriptor);
}

static inline ku_result_t ku_service_register(
    const char* name,
    size_t name_size) {
    const ku_service_descriptor descriptor = {
        sizeof(ku_service_descriptor),
        KU_SERVICE_DESCRIPTOR_ABI_VERSION,
        KU_SERVICE_DEFAULT_VERSION,
        KU_SERVICE_DEFAULT_VERSION,
        UINT64_C(0),
        UINT64_C(0)
    };
    return ku_service_register_versioned(name, name_size, &descriptor);
}

static inline ku_result_t ku_service_connect_versioned(
    const char* name,
    size_t name_size,
    ku_service_negotiation* negotiation) {
    if (negotiation == NULL) return KU_STATUS_INVALID_ARGUMENT;
    return ku_syscall3(
        KU_SYS_IPC_CONNECT,
        (uint64_t)(uintptr_t)name,
        (uint64_t)name_size,
        (uint64_t)(uintptr_t)negotiation);
}

static inline ku_result_t ku_service_connect(
    const char* name,
    size_t name_size) {
    ku_service_negotiation negotiation = {
        sizeof(ku_service_negotiation),
        KU_SERVICE_NEGOTIATION_ABI_VERSION,
        KU_SERVICE_DEFAULT_VERSION,
        KU_SERVICE_DEFAULT_VERSION,
        0U, 0U, 0U, 0U, UINT64_C(0), UINT64_C(0)
    };
    return ku_service_connect_versioned(name, name_size, &negotiation);
}

static inline ku_status_t ku_service_query(
    const char* name,
    size_t name_size,
    ku_service_info* info) {
    if (info == NULL) return KU_STATUS_INVALID_ARGUMENT;
    info->structure_size = sizeof(*info);
    info->abi_version = KU_SERVICE_INFO_ABI_VERSION;
    info->service_version = 0U;
    info->minimum_client_version = 0U;
    info->capabilities = UINT64_C(0);
    info->owner_pid = UINT64_C(0);
    return (ku_status_t)ku_syscall3(
        KU_SYS_IPC_QUERY,
        (uint64_t)(uintptr_t)name,
        (uint64_t)name_size,
        (uint64_t)(uintptr_t)info);
}

static inline ku_result_t ku_service_accept(ku_service_endpoint_t endpoint) {
    return ku_ipc_accept(endpoint);
}

static inline ku_status_t ku_service_send(
    ku_service_connection_t connection,
    const void* data,
    size_t size) {
    return ku_ipc_send(connection, data, size);
}

static inline ku_status_t ku_service_receive(
    ku_service_connection_t connection,
    ku_service_message* message) {
    return ku_ipc_receive(connection, message);
}

static inline ku_status_t ku_service_close(ku_ipc_handle_t handle) {
    return ku_ipc_close(handle);
}

#endif

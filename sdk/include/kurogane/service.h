#ifndef KUROGANE_SDK_SERVICE_H
#define KUROGANE_SDK_SERVICE_H

#include <kurogane/ipc.h>

/*
 * KuroganeOS 3.4 service architecture foundation.
 *
 * A service endpoint is not a second registry layered over IPC. The existing
 * kernel IPC endpoint table remains the single source of truth for service
 * names, process ownership, pending connections and generation-safe handles.
 * These helpers provide service-oriented names for that real transport.
 */
#define KU_SERVICE_NAME_CAPACITY KU_IPC_SERVICE_NAME_CAPACITY
#define KU_SERVICE_MESSAGE_CAPACITY KU_IPC_MESSAGE_CAPACITY

typedef ku_ipc_handle_t ku_service_endpoint_t;
typedef ku_ipc_handle_t ku_service_connection_t;
typedef ku_ipc_message ku_service_message;

static inline ku_result_t ku_service_register(
    const char* name,
    size_t name_size) {
    return ku_ipc_bind(name, name_size);
}

static inline ku_result_t ku_service_connect(
    const char* name,
    size_t name_size) {
    return ku_ipc_connect(name, name_size);
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

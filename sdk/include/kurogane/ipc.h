#ifndef KUROGANE_SDK_IPC_H
#define KUROGANE_SDK_IPC_H

#include <kurogane/syscall.h>

#define KU_IPC_SERVICE_NAME_CAPACITY 32U
#define KU_IPC_MESSAGE_CAPACITY 256U

typedef ku_handle_t ku_ipc_handle_t;

typedef struct ku_ipc_message {
    uint32_t structure_size;
    uint32_t data_size;
    uint64_t sender_pid;
    uint8_t data[KU_IPC_MESSAGE_CAPACITY];
} ku_ipc_message;

#if defined(__cplusplus)
static_assert(sizeof(ku_ipc_message) == 272, "IPC message ABI mismatch");
#else
_Static_assert(sizeof(ku_ipc_message) == 272, "IPC message ABI mismatch");
#endif

static inline ku_result_t ku_ipc_bind(const char* name, size_t name_size) {
    return ku_syscall3(
        KU_SYS_IPC_BIND,
        (uint64_t)(uintptr_t)name,
        (uint64_t)name_size,
        0U);
}

static inline ku_result_t ku_ipc_connect(const char* name, size_t name_size) {
    return ku_syscall3(
        KU_SYS_IPC_CONNECT,
        (uint64_t)(uintptr_t)name,
        (uint64_t)name_size,
        0U);
}

static inline ku_result_t ku_ipc_accept(ku_ipc_handle_t endpoint) {
    return ku_syscall3(KU_SYS_IPC_ACCEPT, endpoint, 0U, 0U);
}

static inline ku_status_t ku_ipc_send(
    ku_ipc_handle_t channel,
    const void* data,
    size_t size) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_IPC_SEND,
        channel,
        (uint64_t)(uintptr_t)data,
        (uint64_t)size);
}

static inline ku_status_t ku_ipc_receive(
    ku_ipc_handle_t channel,
    ku_ipc_message* message) {
    if (message == NULL) return KU_STATUS_INVALID_ARGUMENT;
    message->structure_size = sizeof(*message);
    message->data_size = 0U;
    message->sender_pid = 0U;
    return (ku_status_t)ku_syscall3(
        KU_SYS_IPC_RECEIVE,
        channel,
        (uint64_t)(uintptr_t)message,
        sizeof(*message));
}

static inline ku_status_t ku_ipc_close(ku_ipc_handle_t handle) {
    return (ku_status_t)ku_syscall3(KU_SYS_IPC_CLOSE, handle, 0U, 0U);
}

#endif

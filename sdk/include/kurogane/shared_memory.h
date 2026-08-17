#ifndef KUROGANE_SDK_SHARED_MEMORY_H
#define KUROGANE_SDK_SHARED_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#include <kurogane/status.h>
#include <kurogane/syscall.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KU_SHM_PAGE_SIZE ((size_t)4096U)
#define KU_SHM_MAX_SIZE ((size_t)(16U * KU_SHM_PAGE_SIZE))

typedef uint64_t ku_shm_handle_t;

/* Create a zero-filled object owned by the calling process. */
static inline ku_result_t ku_shm_create(size_t size) {
    if (size == 0U || size > KU_SHM_MAX_SIZE) return KU_STATUS_OUT_OF_RANGE;
    return ku_syscall3(KU_SYS_SHM_CREATE, (uint64_t)size, 0U, 0U);
}

/* Allow target_pid to map this object. Only the owner may grant access. */
static inline ku_status_t ku_shm_grant(ku_shm_handle_t handle, uint64_t target_pid) {
    if (handle == 0U || target_pid == 0U) return KU_STATUS_INVALID_ARGUMENT;
    return (ku_status_t)ku_syscall3(KU_SYS_SHM_GRANT, handle, target_pid, 0U);
}

/* Returns a writable, NX userspace address or a negative status. */
static inline ku_result_t ku_shm_map(ku_shm_handle_t handle) {
    if (handle == 0U) return KU_STATUS_INVALID_ARGUMENT;
    return ku_syscall3(KU_SYS_SHM_MAP, handle, 0U, 0U);
}

static inline ku_status_t ku_shm_unmap(ku_shm_handle_t handle) {
    if (handle == 0U) return KU_STATUS_INVALID_ARGUMENT;
    return (ku_status_t)ku_syscall3(KU_SYS_SHM_UNMAP, handle, 0U, 0U);
}

/* Drops this process' open reference; existing mappings must still be unmapped. */
static inline ku_status_t ku_shm_close(ku_shm_handle_t handle) {
    if (handle == 0U) return KU_STATUS_INVALID_ARGUMENT;
    return (ku_status_t)ku_syscall3(KU_SYS_SHM_CLOSE, handle, 0U, 0U);
}

#ifdef __cplusplus
}
#endif

#endif

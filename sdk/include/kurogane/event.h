#ifndef KUROGANE_SDK_EVENT_H
#define KUROGANE_SDK_EVENT_H

#include <stdint.h>

#include <kurogane/status.h>
#include <kurogane/syscall.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t ku_event_handle_t;

enum ku_event_reset_mode {
    KU_EVENT_AUTO_RESET = 0,
    KU_EVENT_MANUAL_RESET = 1
};

static inline ku_result_t ku_event_create(
    uint32_t reset_mode,
    int initially_signaled) {
    if (reset_mode > KU_EVENT_MANUAL_RESET ||
        (initially_signaled != 0 && initially_signaled != 1)) {
        return KU_STATUS_INVALID_ARGUMENT;
    }
    return ku_syscall3(
        KU_SYS_EVENT_CREATE,
        reset_mode,
        (uint64_t)(uint32_t)initially_signaled,
        0U);
}

static inline ku_status_t ku_event_grant(
    ku_event_handle_t handle,
    uint64_t target_pid) {
    if (handle == 0U || target_pid == 0U) return KU_STATUS_INVALID_ARGUMENT;
    return (ku_status_t)ku_syscall3(KU_SYS_EVENT_GRANT, handle, target_pid, 0U);
}

static inline ku_status_t ku_event_signal(ku_event_handle_t handle) {
    if (handle == 0U) return KU_STATUS_INVALID_ARGUMENT;
    return (ku_status_t)ku_syscall3(KU_SYS_EVENT_SIGNAL, handle, 0U, 0U);
}

static inline ku_status_t ku_event_reset(ku_event_handle_t handle) {
    if (handle == 0U) return KU_STATUS_INVALID_ARGUMENT;
    return (ku_status_t)ku_syscall3(KU_SYS_EVENT_RESET, handle, 0U, 0U);
}

/* Non-blocking foundation. KU_STATUS_WOULD_BLOCK means not signaled yet. */
static inline ku_status_t ku_event_poll(ku_event_handle_t handle) {
    if (handle == 0U) return KU_STATUS_INVALID_ARGUMENT;
    return (ku_status_t)ku_syscall3(KU_SYS_EVENT_POLL, handle, 0U, 0U);
}

static inline ku_status_t ku_event_close(ku_event_handle_t handle) {
    if (handle == 0U) return KU_STATUS_INVALID_ARGUMENT;
    return (ku_status_t)ku_syscall3(KU_SYS_EVENT_CLOSE, handle, 0U, 0U);
}

#ifdef __cplusplus
}
#endif

#endif

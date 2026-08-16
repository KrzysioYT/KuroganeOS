#ifndef KUROGANE_SDK_DESKTOP_H
#define KUROGANE_SDK_DESKTOP_H

#include <stdint.h>
#include <kurogane/status.h>
#include <kurogane/syscall.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ku_desktop_app_id {
    KU_DESKTOP_APP_HOME = 0,
    KU_DESKTOP_APP_TERMINAL = 1,
    KU_DESKTOP_APP_FILES = 2,
    KU_DESKTOP_APP_PERFORMANCE = 3,
    KU_DESKTOP_APP_BROWSER = 4,
    KU_DESKTOP_APP_MONITOR = 5,
    KU_DESKTOP_APP_SETTINGS = 6,
    KU_DESKTOP_APP_ABOUT = 7,
    KU_DESKTOP_APP_COUNT = 8
};

enum ku_desktop_pin_action {
    KU_DESKTOP_PIN_QUERY = 0,
    KU_DESKTOP_PIN_SET = 1,
    KU_DESKTOP_PIN_TOGGLE = 2
};

typedef struct ku_desktop_pin_request {
    uint32_t structure_size;
    uint32_t app_id;
    uint32_t action;
    uint32_t value;
    uint32_t pinned;
    uint32_t reserved;
} ku_desktop_pin_request;

static inline ku_status_t ku_desktop_pin(ku_desktop_pin_request* request) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_DESKTOP_PIN,
        (uint64_t)(uintptr_t)request,
        (uint64_t)sizeof(ku_desktop_pin_request),
        0U);
}

#ifdef __cplusplus
}
#endif
#endif

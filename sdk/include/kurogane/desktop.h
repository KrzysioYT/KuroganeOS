#ifndef KUROGANE_SDK_DESKTOP_H
#define KUROGANE_SDK_DESKTOP_H

#include <stdint.h>
#include <kurogane/status.h>
#include <kurogane/design.h>
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
    KU_DESKTOP_PIN_TOGGLE = 2,
    // Session-ready query: app_id must be HOME. 'pinned' reports whether
    // a live, visible Home window has submitted its first UI surface.
    // This cannot modify desktop pin state or launch an app.
    KU_DESKTOP_PIN_HOME_READY = 3,
    // Privileged to the live Home process. Focus an existing app window only;
    // never spawn an executable or alter its pinning state.
    KU_DESKTOP_PIN_FOCUS_RUNNING = 4,
    // Read-only: app window has presented a native/legacy UI surface.
    // A minimized app is still ready. "pinned" is the returned readiness
    // flag; the on-disk pin state is untouched.
    KU_DESKTOP_PIN_APP_READY = 5
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


enum ku_desktop_style_action { KU_DESKTOP_STYLE_QUERY = 0, KU_DESKTOP_STYLE_SET = 1 };
typedef struct ku_desktop_style_request {
    uint32_t structure_size, action, style_id, reserved;
} ku_desktop_style_request;
static inline ku_status_t ku_desktop_style(ku_desktop_style_request* request) {
    return (ku_status_t)ku_syscall3(KU_SYS_DESKTOP_STYLE,
        (uint64_t)(uintptr_t)request, (uint64_t)sizeof(*request), 0U);
}

enum ku_desktop_theme_action {
    KU_DESKTOP_THEME_QUERY = 0,
    KU_DESKTOP_THEME_SET = 1
};
typedef struct ku_desktop_theme_request {
    uint32_t structure_size;
    uint32_t action;
    uint32_t theme_id;
    uint32_t reserved;
} ku_desktop_theme_request;

static inline ku_status_t ku_desktop_theme(ku_desktop_theme_request* request) {
    return (ku_status_t)ku_syscall3(
        KU_SYS_DESKTOP_THEME,
        (uint64_t)(uintptr_t)request,
        (uint64_t)sizeof(ku_desktop_theme_request), 0U);
}

#ifdef __cplusplus
}
#endif
#endif

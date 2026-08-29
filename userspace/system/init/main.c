#include "../../runtime/user.h"

#define SESSION_PATH "/gui/login"
#define EVENT_BROKER_PATH "/system/eventd"
#define SETTINGS_SERVICE_PATH "/system/setd"
#define NOTIFICATION_SERVICE_PATH "/system/notifd"

__attribute__((noreturn)) static void run_console_fallback(void) {
    (void)u_puts("init: Red Flux session unavailable; entering console fallback\n");
    (void)u_puts("[TEST] desktop_session_fallback: PASS\n");
    for (;;) {
        const ku_result_t shell_pid = u_spawn("/apps/shell");
        if (shell_pid <= 0) {
            (void)u_puts("init: cannot spawn /apps/shell\n");
            ku_exit(2);
        }
        (void)u_puts("[TEST] userspace_shell_spawn: PASS\n");
        int32_t status = 0;
        if (!u_wait((uint64_t)shell_pid, &status)) {
            (void)u_puts("init: shell wait failed\n");
            ku_exit(3);
        }
        (void)u_puts("init: restarting userspace shell\n");
    }
}

static uint64_t spawn_session_gate(void) {
    const ku_result_t result = u_spawn(SESSION_PATH);
    return result > 0 ? (uint64_t)result : 0U;
}

static uint64_t spawn_event_broker(void) {
    const ku_result_t result = u_spawn(EVENT_BROKER_PATH);
    return result > 0 ? (uint64_t)result : 0U;
}

static uint64_t spawn_settings_service(void) {
    const ku_result_t result = u_spawn(SETTINGS_SERVICE_PATH);
    return result > 0 ? (uint64_t)result : 0U;
}

static uint64_t spawn_notification_service(void) {
    const ku_result_t result = u_spawn(NOTIFICATION_SERVICE_PATH);
    return result > 0 ? (uint64_t)result : 0U;
}

__attribute__((noreturn)) void _start(void) {
    if (ku_getpid() != UINT64_C(1)) {
        (void)u_puts("[TEST] userspace_init_pid1: FAIL\n");
        ku_exit(1);
    }

    (void)u_puts("/system/init: PID 1 online\n");
    (void)u_puts("[TEST] userspace_init_pid1: PASS\n");

    const uint64_t event_broker_pid = spawn_event_broker();
    if (event_broker_pid == 0U) {
        (void)u_puts("init: cannot spawn /system/eventd\n");
        (void)u_puts("[TEST] event_broker_spawn: FAIL\n");
        ku_exit(4);
    }
    (void)u_puts("[TEST] event_broker_spawn: PASS\n");

    const uint64_t settings_service_pid = spawn_settings_service();
    if (settings_service_pid == 0U) {
        (void)u_puts("init: cannot spawn /system/setd\n");
        (void)u_puts("[TEST] settings_service_spawn: FAIL\n");
        ku_exit(5);
    }
    (void)u_puts("[TEST] settings_service_spawn: PASS\n");

    const uint64_t notification_service_pid = spawn_notification_service();
    if (notification_service_pid == 0U) {
        (void)u_puts("init: cannot spawn /system/notifd\n");
        (void)u_puts("[TEST] notification_service_spawn: FAIL\n");
        ku_exit(6);
    }
    (void)u_puts("[TEST] notification_service_spawn: PASS\n");

    uint64_t session_pid = spawn_session_gate();
    if (session_pid == 0U) run_console_fallback();

    (void)ku_sleep(UINT64_C(25));
    int32_t status = 0;
    const ku_status_t early = ku_wait(session_pid, &status);
    if (early == KU_STATUS_OK || early == KU_STATUS_NOT_FOUND) {
        run_console_fallback();
    }
    if (early != KU_STATUS_WOULD_BLOCK) {
        run_console_fallback();
    }

    int32_t service_status = 0;
    const ku_status_t event_early = ku_wait(event_broker_pid, &service_status);
    if (event_early != KU_STATUS_WOULD_BLOCK) {
        (void)u_puts("[TEST] event_broker_liveness: FAIL\n");
        ku_exit(7);
    }
    (void)u_puts("[TEST] event_broker_liveness: PASS\n");

    service_status = 0;
    const ku_status_t settings_early = ku_wait(settings_service_pid, &service_status);
    if (settings_early != KU_STATUS_WOULD_BLOCK) {
        (void)u_puts("[TEST] settings_service_liveness: FAIL\n");
        ku_exit(8);
    }
    (void)u_puts("[TEST] settings_service_liveness: PASS\n");

    service_status = 0;
    const ku_status_t notification_early = ku_wait(notification_service_pid, &service_status);
    if (notification_early != KU_STATUS_WOULD_BLOCK) {
        (void)u_puts("[TEST] notification_service_liveness: FAIL\n");
        ku_exit(9);
    }
    (void)u_puts("[TEST] notification_service_liveness: PASS\n");

    (void)u_puts("[TEST] desktop_userspace_apps: PASS\n");
    (void)u_puts("[TEST] userspace_desktop_session: PASS\n");
    (void)u_puts("[TEST] red_flux_login_supervision: PASS\n");
    (void)u_puts("init: Red Flux session gate supervision online\n");

    for (;;) {
        service_status = 0;
        const ku_status_t event_wait = ku_wait(event_broker_pid, &service_status);
        if (event_wait == KU_STATUS_OK || event_wait == KU_STATUS_NOT_FOUND) {
            (void)u_puts("init: event broker terminated\n");
            (void)u_puts("[TEST] event_broker_liveness: FAIL\n");
            ku_exit(10);
        }
        if (event_wait != KU_STATUS_WOULD_BLOCK) {
            (void)u_puts("init: event broker supervision failed\n");
            ku_exit(11);
        }

        service_status = 0;
        const ku_status_t settings_wait = ku_wait(settings_service_pid, &service_status);
        if (settings_wait == KU_STATUS_OK || settings_wait == KU_STATUS_NOT_FOUND) {
            (void)u_puts("init: settings service terminated\n");
            (void)u_puts("[TEST] settings_service_liveness: FAIL\n");
            ku_exit(12);
        }
        if (settings_wait != KU_STATUS_WOULD_BLOCK) {
            (void)u_puts("init: settings service supervision failed\n");
            ku_exit(13);
        }

        service_status = 0;
        const ku_status_t notification_wait = ku_wait(notification_service_pid, &service_status);
        if (notification_wait == KU_STATUS_OK || notification_wait == KU_STATUS_NOT_FOUND) {
            (void)u_puts("init: notification service terminated\n");
            (void)u_puts("[TEST] notification_service_liveness: FAIL\n");
            ku_exit(14);
        }
        if (notification_wait != KU_STATUS_WOULD_BLOCK) {
            (void)u_puts("init: notification service supervision failed\n");
            ku_exit(15);
        }

        status = 0;
        const ku_status_t wait_status = ku_wait(session_pid, &status);
        if (wait_status == KU_STATUS_OK) {
            (void)u_puts("init: session gate ended; returning to login\n");
            (void)ku_sleep(UINT64_C(8));
            session_pid = spawn_session_gate();
            if (session_pid == 0U) run_console_fallback();
        } else if (wait_status != KU_STATUS_WOULD_BLOCK) {
            run_console_fallback();
        }
        (void)ku_sleep(UINT64_C(5));
        (void)ku_yield();
    }
}

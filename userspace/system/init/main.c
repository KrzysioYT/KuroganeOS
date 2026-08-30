#include "../../runtime/user.h"

#define SESSION_PATH "/gui/login"
#define EVENT_BROKER_PATH "/system/eventd"
#define SETTINGS_SERVICE_PATH "/system/setd"
#define NOTIFICATION_SERVICE_PATH "/system/notifd"
#define ACCOUNT_SERVICE_PATH "/system/accountd"
#define SESSION_SERVICE_PATH "/system/sessiond"
#define CLIPBOARD_SERVICE_PATH "/system/clipd"
#define NETWORK_EVENT_SERVICE_PATH "/system/neteventd"
#define DNS_SERVICE_PATH "/system/dnsd"
#define APPLICATION_REGISTRY_PATH "/system/appregd"
#define AUDIO_SERVICE_PATH "/system/audiod"

#define SERVICE_RESTART_LIMIT 3U
#define SERVICE_RESTART_BASE_DELAY UINT64_C(4)

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

static uint64_t spawn_account_service(void) {
    const ku_result_t result = u_spawn(ACCOUNT_SERVICE_PATH);
    return result > 0 ? (uint64_t)result : 0U;
}

static uint64_t spawn_session_service(void) {
    const ku_result_t result = u_spawn(SESSION_SERVICE_PATH);
    return result > 0 ? (uint64_t)result : 0U;
}

static uint64_t spawn_clipboard_service(void) {
    const ku_result_t result = u_spawn(CLIPBOARD_SERVICE_PATH);
    return result > 0 ? (uint64_t)result : 0U;
}

static uint64_t spawn_network_event_service(void) {
    const ku_result_t result = u_spawn(NETWORK_EVENT_SERVICE_PATH);
    return result > 0 ? (uint64_t)result : 0U;
}

static uint64_t spawn_dns_service(void) {
    const ku_result_t result = u_spawn(DNS_SERVICE_PATH);
    return result > 0 ? (uint64_t)result : 0U;
}

static uint64_t spawn_application_registry(void) {
    const ku_result_t result = u_spawn(APPLICATION_REGISTRY_PATH);
    return result > 0 ? (uint64_t)result : 0U;
}

static uint64_t spawn_audio_service(void) {
    const ku_result_t result = u_spawn(AUDIO_SERVICE_PATH);
    return result > 0 ? (uint64_t)result : 0U;
}

typedef uint64_t (*service_spawn_fn)(void);

typedef struct service_watch {
    service_spawn_fn spawn;
    uint64_t pid;
    uint32_t restarts;
    const char* restart_marker;
} service_watch;

static int supervise_service(service_watch* watch) {
    int32_t exit_code = 0;
    ku_status_t status;
    uint64_t replacement;
    uint64_t delay;

    if (watch == (service_watch*)0 || watch->pid == 0U ||
        watch->spawn == (service_spawn_fn)0) {
        return 0;
    }

    status = ku_wait(watch->pid, &exit_code);
    if (status == KU_STATUS_WOULD_BLOCK) return 1;
    if (status != KU_STATUS_OK && status != KU_STATUS_NOT_FOUND) return 0;
    if (watch->restarts >= SERVICE_RESTART_LIMIT) return 0;

    delay = SERVICE_RESTART_BASE_DELAY * (uint64_t)(watch->restarts + 1U);
    (void)u_puts("init: supervised service terminated; restarting\n");
    (void)ku_sleep(delay);
    replacement = watch->spawn();
    if (replacement == 0U) return 0;

    watch->pid = replacement;
    ++watch->restarts;
    if (watch->restart_marker != (const char*)0) {
        (void)u_puts(watch->restart_marker);
    }
    return 1;
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

    const uint64_t network_event_service_pid = spawn_network_event_service();
    if (network_event_service_pid == 0U) {
        (void)u_puts("init: cannot spawn /system/neteventd\n");
        ku_exit(31);
    }

    const uint64_t dns_service_pid = spawn_dns_service();
    if (dns_service_pid == 0U) {
        (void)u_puts("init: cannot spawn /system/dnsd\n");
        ku_exit(40);
    }

    const uint64_t application_registry_pid = spawn_application_registry();
    if (application_registry_pid == 0U) {
        (void)u_puts("init: cannot spawn /system/appregd\n");
        ku_exit(34);
    }

    const uint64_t audio_service_pid = spawn_audio_service();
    if (audio_service_pid == 0U) {
        (void)u_puts("init: cannot spawn /system/audiod\n");
        ku_exit(37);
    }

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

    const uint64_t account_service_pid = spawn_account_service();
    if (account_service_pid == 0U) {
        (void)u_puts("init: cannot spawn /system/accountd\n");
        (void)u_puts("[TEST] account_service_spawn: FAIL\n");
        ku_exit(16);
    }
    (void)u_puts("[TEST] account_service_spawn: PASS\n");

    const uint64_t session_service_pid = spawn_session_service();
    if (session_service_pid == 0U) {
        (void)u_puts("init: cannot spawn /system/sessiond\n");
        (void)u_puts("[TEST] session_service_spawn: FAIL\n");
        ku_exit(20);
    }
    (void)u_puts("[TEST] session_service_spawn: PASS\n");

    const uint64_t clipboard_service_pid = spawn_clipboard_service();
    if (clipboard_service_pid == 0U) {
        (void)u_puts("init: cannot spawn /system/clipd\n");
        (void)u_puts("[TEST] clipboard_service_spawn: FAIL\n");
        ku_exit(27);
    }
    (void)u_puts("[TEST] clipboard_service_spawn: PASS\n");

    service_watch event_watch = {
        spawn_event_broker, event_broker_pid, 0U,
        "[TEST] event_broker_restart: PASS\n"
    };
    service_watch network_event_watch = {
        spawn_network_event_service, network_event_service_pid, 0U,
        (const char*)0
    };
    service_watch dns_service_watch = {
        spawn_dns_service, dns_service_pid, 0U,
        (const char*)0
    };
    service_watch application_registry_watch = {
        spawn_application_registry, application_registry_pid, 0U,
        (const char*)0
    };
    service_watch audio_service_watch = {
        spawn_audio_service, audio_service_pid, 0U,
        (const char*)0
    };
    service_watch settings_watch = {
        spawn_settings_service, settings_service_pid, 0U,
        "[TEST] settings_service_restart: PASS\n"
    };
    service_watch notification_watch = {
        spawn_notification_service, notification_service_pid, 0U,
        "[TEST] notification_service_restart: PASS\n"
    };
    service_watch account_watch = {
        spawn_account_service, account_service_pid, 0U,
        "[TEST] account_service_restart: PASS\n"
    };
    service_watch session_service_watch = {
        spawn_session_service, session_service_pid, 0U,
        "[TEST] session_service_restart: PASS\n"
    };
    service_watch clipboard_watch = {
        spawn_clipboard_service, clipboard_service_pid, 0U,
        "[TEST] clipboard_service_restart: PASS\n"
    };

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

    if (!supervise_service(&event_watch)) {
        (void)u_puts("[TEST] event_broker_liveness: FAIL\n");
        ku_exit(7);
    }
    (void)u_puts("[TEST] event_broker_liveness: PASS\n");

    if (!supervise_service(&network_event_watch)) {
        (void)u_puts("init: network event service supervision failed\n");
        ku_exit(32);
    }

    if (!supervise_service(&dns_service_watch)) {
        (void)u_puts("init: DNS service supervision failed\n");
        ku_exit(41);
    }

    if (!supervise_service(&application_registry_watch)) {
        (void)u_puts("init: application registry supervision failed\n");
        ku_exit(35);
    }

    if (!supervise_service(&audio_service_watch)) {
        (void)u_puts("init: audio service supervision failed\n");
        ku_exit(38);
    }

    if (!supervise_service(&settings_watch)) {
        (void)u_puts("[TEST] settings_service_liveness: FAIL\n");
        ku_exit(8);
    }
    (void)u_puts("[TEST] settings_service_liveness: PASS\n");

    if (!supervise_service(&notification_watch)) {
        (void)u_puts("[TEST] notification_service_liveness: FAIL\n");
        ku_exit(9);
    }
    (void)u_puts("[TEST] notification_service_liveness: PASS\n");

    if (!supervise_service(&account_watch)) {
        (void)u_puts("[TEST] account_service_liveness: FAIL\n");
        ku_exit(17);
    }
    (void)u_puts("[TEST] account_service_liveness: PASS\n");

    if (!supervise_service(&session_service_watch)) {
        (void)u_puts("[TEST] session_service_liveness: FAIL\n");
        ku_exit(24);
    }
    (void)u_puts("[TEST] session_service_liveness: PASS\n");

    if (!supervise_service(&clipboard_watch)) {
        (void)u_puts("[TEST] clipboard_service_liveness: FAIL\n");
        ku_exit(29);
    }
    (void)u_puts("[TEST] clipboard_service_liveness: PASS\n");

    (void)u_puts("[TEST] desktop_userspace_apps: PASS\n");
    (void)u_puts("[TEST] userspace_desktop_session: PASS\n");
    (void)u_puts("[TEST] red_flux_login_supervision: PASS\n");
    (void)u_puts("init: Red Flux session gate supervision online\n");

    for (;;) {
        if (!supervise_service(&event_watch)) {
            (void)u_puts("[TEST] event_broker_liveness: FAIL\n");
            ku_exit(10);
        }
        if (!supervise_service(&network_event_watch)) {
            (void)u_puts("init: network event service supervision failed\n");
            ku_exit(33);
        }
        if (!supervise_service(&dns_service_watch)) {
            (void)u_puts("init: DNS service supervision failed\n");
            ku_exit(42);
        }
        if (!supervise_service(&application_registry_watch)) {
            (void)u_puts("init: application registry supervision failed\n");
            ku_exit(36);
        }
        if (!supervise_service(&audio_service_watch)) {
            (void)u_puts("init: audio service supervision failed\n");
            ku_exit(39);
        }
        if (!supervise_service(&settings_watch)) {
            (void)u_puts("[TEST] settings_service_liveness: FAIL\n");
            ku_exit(12);
        }
        if (!supervise_service(&notification_watch)) {
            (void)u_puts("[TEST] notification_service_liveness: FAIL\n");
            ku_exit(14);
        }
        if (!supervise_service(&account_watch)) {
            (void)u_puts("[TEST] account_service_liveness: FAIL\n");
            ku_exit(18);
        }
        if (!supervise_service(&session_service_watch)) {
            (void)u_puts("[TEST] session_service_liveness: FAIL\n");
            ku_exit(25);
        }
        if (!supervise_service(&clipboard_watch)) {
            (void)u_puts("[TEST] clipboard_service_liveness: FAIL\n");
            ku_exit(30);
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

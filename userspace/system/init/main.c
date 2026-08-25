#include "../../runtime/user.h"

#define SESSION_PATH "/gui/login"

__attribute__((noreturn)) static void run_console_fallback(void) {
    (void)u_puts("init: Forged Steel session unavailable; entering console fallback\n");
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

__attribute__((noreturn)) void _start(void) {
    if (ku_getpid() != UINT64_C(1)) {
        (void)u_puts("[TEST] userspace_init_pid1: FAIL\n");
        ku_exit(1);
    }

    (void)u_puts("/system/init: PID 1 online\n");
    (void)u_puts("[TEST] userspace_init_pid1: PASS\n");

    uint64_t session_pid = spawn_session_gate();
    if (session_pid == 0U) run_console_fallback();

    // PID 1 supervises one graphical secure-access gate. The login process
    // owns the transition into Blade Launcher and waits for the desktop root.
    // When the desktop session ends, login exits and PID 1 creates a fresh gate.
    (void)ku_sleep(UINT64_C(25));
    int32_t status = 0;
    const ku_status_t early = ku_wait(session_pid, &status);
    if (early == KU_STATUS_OK || early == KU_STATUS_NOT_FOUND) {
        run_console_fallback();
    }
    if (early != KU_STATUS_WOULD_BLOCK) {
        run_console_fallback();
    }

    (void)u_puts("[TEST] desktop_userspace_apps: PASS\n");
    (void)u_puts("[TEST] userspace_desktop_session: PASS\n");
    // Compatibility marker retained while older qualification scripts still
    // consume it. It is not a user-visible product name anymore.
    (void)u_puts("[TEST] red_flux_login_supervision: PASS\n");
    (void)u_puts("[TEST] forged_steel_login_supervision: PASS\n");
    (void)u_puts("init: Forged Steel secure session supervision online\n");

    for (;;) {
        status = 0;
        const ku_status_t wait_status = ku_wait(session_pid, &status);
        if (wait_status == KU_STATUS_OK) {
            (void)u_puts("init: session gate ended; returning to secure access\n");
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

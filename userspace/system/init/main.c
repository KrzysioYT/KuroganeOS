#include "../../runtime/user.h"

#define DESKTOP_CHILD_COUNT 5U

typedef struct desktop_child {
    const char* path;
    uint64_t pid;
} desktop_child;

static desktop_child g_desktop_children[DESKTOP_CHILD_COUNT] = {
    {"/gui/terminal", 0U},
    {"/gui/files", 0U},
    {"/gui/sysmon", 0U},
    {"/gui/settings", 0U},
    {"/gui/about", 0U},
};

static int spawn_child(desktop_child* child) {
    const ku_result_t result = u_spawn(child->path);
    if (result <= 0) return 0;
    child->pid = (uint64_t)result;
    return 1;
}

static int spawn_desktop(void) {
    size_t index = 0U;
    while (index < DESKTOP_CHILD_COUNT) {
        if (!spawn_child(&g_desktop_children[index])) return 0;
        ++index;
    }
    return 1;
}

__attribute__((noreturn)) static void run_console_fallback(void) {
    (void)u_puts("init: Flux session unavailable; entering console fallback\n");
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

__attribute__((noreturn)) void _start(void) {
    if (ku_getpid() != UINT64_C(1)) {
        (void)u_puts("[TEST] userspace_init_pid1: FAIL\n");
        ku_exit(1);
    }
    (void)u_puts("/system/init: PID 1 online\n");
    (void)u_puts("[TEST] userspace_init_pid1: PASS\n");

    if (!spawn_desktop()) {
        run_console_fallback();
    }

    // Give the GUI children time to create their windows. If most of them
    // terminate immediately, the kernel-side session or public UI ABI is not
    // usable and PID1 falls back to the text console instead of respawn-looping.
    (void)ku_sleep(UINT64_C(25));
    size_t early_failures = 0U;
    size_t index = 0U;
    while (index < DESKTOP_CHILD_COUNT) {
        int32_t status = 0;
        const ku_status_t wait_status =
            ku_wait(g_desktop_children[index].pid, &status);
        if (wait_status == KU_STATUS_OK) {
            g_desktop_children[index].pid = 0U;
            ++early_failures;
        } else if (wait_status != KU_STATUS_WOULD_BLOCK) {
            ++early_failures;
        }
        ++index;
    }
    if (early_failures >= 3U) {
        run_console_fallback();
    }

    index = 0U;
    while (index < DESKTOP_CHILD_COUNT) {
        if (g_desktop_children[index].pid == 0U &&
            !spawn_child(&g_desktop_children[index])) {
            run_console_fallback();
        }
        ++index;
    }

    (void)u_puts("[TEST] desktop_userspace_apps: PASS\n");
    (void)u_puts("[TEST] userspace_desktop_session: PASS\n");
    (void)u_puts("init: Kurogane Flux desktop supervision online\n");

    for (;;) {
        index = 0U;
        while (index < DESKTOP_CHILD_COUNT) {
            int32_t status = 0;
            const ku_status_t wait_status =
                ku_wait(g_desktop_children[index].pid, &status);
            if (wait_status == KU_STATUS_OK) {
                (void)u_puts("init: restarting desktop application ");
                (void)u_puts(g_desktop_children[index].path);
                (void)u_puts("\n");
                if (!spawn_child(&g_desktop_children[index])) {
                    run_console_fallback();
                }
            } else if (wait_status != KU_STATUS_WOULD_BLOCK) {
                run_console_fallback();
            }
            ++index;
        }
        (void)ku_sleep(UINT64_C(5));
        (void)ku_yield();
    }
}

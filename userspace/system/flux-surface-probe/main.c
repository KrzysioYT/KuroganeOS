#include "../../runtime/user.h"

#define CHURN_ITERATIONS 20U
#define WORKER_PATH "/system/fluxwrk"
#define CRASH_PATH "/system/fluxcrsh"

__attribute__((noreturn)) void _start(void) {
    uint32_t iteration;
    int32_t exit_code = 0;
    ku_result_t crash_pid;

    if (!u_spawn_wait(WORKER_PATH, &exit_code) || exit_code != 0) {
        (void)u_puts("[TEST] flux_retained_surface_present: FAIL\n");
        ku_exit(1);
    }
    (void)u_puts("[TEST] flux_retained_surface_present: PASS\n");

    for (iteration = 1U; iteration < CHURN_ITERATIONS; ++iteration) {
        exit_code = 0;
        if (!u_spawn_wait(WORKER_PATH, &exit_code) || exit_code != 0) {
            (void)u_puts("[TEST] flux_surface_exit_cleanup: FAIL\n");
            ku_exit(2);
        }
    }
    (void)u_puts("[TEST] flux_surface_exit_cleanup: PASS\n");

    crash_pid = u_spawn(CRASH_PATH);
    if (crash_pid <= 0 || !u_wait((uint64_t)crash_pid, &exit_code)) {
        (void)u_puts("[TEST] flux_gui_crash_isolation: FAIL\n");
        ku_exit(3);
    }

    /* The post-crash worker proves both session survival and crash cleanup. */
    exit_code = 0;
    if (!u_spawn_wait(WORKER_PATH, &exit_code) || exit_code != 0) {
        (void)u_puts("[TEST] flux_gui_crash_isolation: FAIL\n");
        ku_exit(4);
    }
    (void)u_puts("[TEST] flux_gui_crash_isolation: PASS\n");
    ku_exit(0);
}

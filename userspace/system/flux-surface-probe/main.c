#include "../../runtime/user.h"

#define CHURN_ITERATIONS 64U
#define WORKER_PATH "/system/fluxwrk"
#define CRASH_PATH "/system/fluxcrsh"

static void report_worker_failure(uint32_t iteration, const char* stage, int32_t exit_code) {
    const int64_t signed_code = (int64_t)exit_code;
    (void)u_puts("[DIAG] flux_surface_worker iteration=");
    (void)u_put_u64(iteration);
    (void)u_puts(" stage=");
    (void)u_puts(stage);
    (void)u_puts(" exit=");
    if (signed_code < 0) {
        (void)u_puts("-");
        (void)u_put_u64((uint64_t)(-signed_code));
    } else {
        (void)u_put_u64((uint64_t)signed_code);
    }
    (void)u_puts("\n");
}

static int run_worker(uint32_t iteration, int32_t* exit_code) {
    const ku_result_t pid = u_spawn(WORKER_PATH);
    if (pid <= 0) {
        report_worker_failure(iteration, "spawn", (int32_t)pid);
        return 0;
    }
    if (!u_wait((uint64_t)pid, exit_code)) {
        report_worker_failure(iteration, "wait", *exit_code);
        return 0;
    }
    if (*exit_code != 0) {
        report_worker_failure(iteration, "exit", *exit_code);
        return 0;
    }
    return 1;
}

__attribute__((noreturn)) void _start(void) {
    uint32_t iteration;
    int32_t exit_code = 0;
    ku_result_t crash_pid;

    if (!run_worker(0U, &exit_code)) {
        (void)u_puts("[TEST] flux_retained_surface_present: FAIL\n");
        ku_exit(1);
    }
    (void)u_puts("[TEST] flux_retained_surface_present: PASS\n");

    for (iteration = 1U; iteration < CHURN_ITERATIONS; ++iteration) {
        exit_code = 0;
        if (!run_worker(iteration, &exit_code)) {
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
    if (!run_worker(CHURN_ITERATIONS, &exit_code)) {
        (void)u_puts("[TEST] flux_gui_crash_isolation: FAIL\n");
        ku_exit(4);
    }
    (void)u_puts("[TEST] flux_gui_crash_isolation: PASS\n");
    ku_exit(0);
}

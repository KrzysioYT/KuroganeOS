#include "../../runtime/user.h"

__attribute__((noreturn)) void _start(void) {
    if (ku_getpid() != UINT64_C(1)) {
        (void)u_puts("[TEST] userspace_init_pid1: FAIL\n");
        ku_exit(1);
    }
    (void)u_puts("/system/init: PID 1 online\n");
    (void)u_puts("[TEST] userspace_init_pid1: PASS\n");

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

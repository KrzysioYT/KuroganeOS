#include "../../runtime/user.h"

__attribute__((noreturn)) void _start(void) {
    const uint64_t pid = ku_getpid();
    const uint64_t tid = ku_gettid();
    if (pid == 0U || tid == 0U) ku_exit(1);
    (void)u_puts("System Monitor [Ring 3] pid=");
    (void)u_put_u64(pid);
    (void)u_puts(" tid=");
    (void)u_put_u64(tid);
    (void)u_puts("\n[TEST] userspace_monitor_app: PASS\n");
    ku_exit(0);
}

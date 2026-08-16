#include <kurogane/kurogane.h>

ku_status_t kuro_sleep(uint64_t ticks) { return ku_sleep(ticks); }
ku_status_t kuro_yield(void) { return ku_yield(); }

ku_status_t kuro_spawn_wait(
    const char* path, size_t path_size, int32_t* exit_code) {
    const ku_result_t child = ku_process_spawn(path, path_size);
    if (child < 0) return (ku_status_t)child;
    for (;;) {
        const ku_status_t status = ku_process_wait((ku_pid_t)child, exit_code);
        if (status == KU_STATUS_OK) return status;
        if (status != KU_STATUS_WOULD_BLOCK) return status;
        (void)ku_yield();
    }
}

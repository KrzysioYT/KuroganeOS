#include "../../runtime/user.h"

__attribute__((noreturn)) void _start(void) {
    static const char path[] = "/etc/system.cfg";
    const ku_result_t opened = ku_open(path, sizeof(path) - 1U, KU_OPEN_READ);
    if (opened <= 0) {
        (void)u_puts("Files: cannot open /etc/system.cfg\n");
        ku_exit(1);
    }
    char buffer[160];
    const ku_result_t count = ku_read((ku_handle_t)opened, buffer, sizeof(buffer));
    const ku_status_t close_status = ku_close((ku_handle_t)opened);
    if (count <= 0 || close_status != KU_STATUS_OK) {
        (void)u_puts("Files: VFS read failed\n");
        ku_exit(2);
    }
    (void)u_puts("Files [Ring 3] /etc/system.cfg:\n");
    (void)u_write_all(buffer, (size_t)count);
    if (buffer[(size_t)count - 1U] != '\n') (void)u_puts("\n");
    (void)u_puts("[TEST] userspace_files_app: PASS\n");
    ku_exit(0);
}

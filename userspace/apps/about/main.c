#include "../../runtime/user.h"
#include "../../../common/version.h"

__attribute__((noreturn)) void _start(void) {
    (void)u_puts("About KuroganeOS " KUROGANE_VERSION_STRING
                 " [Ring 3 application]\n");
    (void)u_puts("[TEST] userspace_about_app: PASS\n");
    ku_exit(0);
}

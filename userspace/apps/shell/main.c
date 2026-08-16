#include "../../common/flux_shell.h"

static flux_shell_state g_shell;

static void console_emit(void* context, const char* line) {
    (void)context;
    (void)u_puts(line);
    (void)u_puts("\n");
}

static const flux_shell_io g_io = {
    console_emit,
    (void*)0,
};

static void banner(void) {
    (void)u_puts("\n");
    (void)u_puts("KUROGANEOS " KUROGANE_VERSION_STRING " / RED FLUX CONSOLE\n");
    (void)u_puts("Ring 3 recovery workspace. Type 'help' for commands.\n");
}

static void prompt(void) {
    flux_shell_reap_jobs(&g_shell, &g_io, 1);
    (void)u_puts("KRG:");
    (void)u_puts(g_shell.cwd);
    (void)u_puts(" > ");
}

static void clear_console(void) {
    for (size_t index = 0U; index < 32U; ++index) (void)u_puts("\n");
    banner();
}

__attribute__((noreturn)) void _start(void) {
    char line[FLUX_SHELL_LINE_CAPACITY];
    size_t length = 0U;

    flux_shell_initialize(&g_shell);
    banner();
    prompt();

    for (;;) {
        char character = 0;
        const ku_result_t result = ku_read(0U, &character, 1U);
        if (result == KU_STATUS_WOULD_BLOCK) {
            (void)ku_yield();
            continue;
        }
        if (result != 1) {
            (void)u_puts("\nshell: stdin failure\n");
            ku_exit(2);
        }

        if (character == '\r' || character == '\n') {
            flux_shell_action action;
            (void)u_puts("\n");
            line[length] = '\0';
            action = flux_shell_execute(&g_shell, &g_io, line);
            length = 0U;
            line[0] = '\0';
            if (action == FLUX_SHELL_ACTION_CLEAR) {
                clear_console();
            } else if (action == FLUX_SHELL_ACTION_EXIT) {
                (void)u_puts("Red Flux console session ended\n");
                ku_exit(g_shell.last_status);
            }
            prompt();
        } else if (character == '\b' || character == 127) {
            if (length != 0U) {
                --length;
                line[length] = '\0';
                (void)u_puts("\b \b");
            }
        } else if (character == 0x15) {
            while (length != 0U) {
                --length;
                (void)u_puts("\b \b");
            }
            line[0] = '\0';
        } else if (character >= 32 && character <= 126 &&
                   length + 1U < sizeof(line)) {
            line[length++] = character;
            line[length] = '\0';
            (void)u_write_all(&character, 1U);
        }
    }
}

#include "../../runtime/user.h"

#define LINE_CAPACITY 96U

static void prompt(void) {
    (void)u_puts("kurogane:user$ ");
}

static void run_child(const char* path) {
    int32_t status = 0;
    if (!u_spawn_wait(path, &status)) {
        (void)u_puts("shell: launch failed\n");
    } else if (status != 0) {
        (void)u_puts("shell: application returned an error\n");
    }
}

static void execute(char* line) {
    if (line[0] == '\0') return;
    if (u_streq(line, "help")) {
        (void)u_puts("help pid echo hello external files monitor about exit run <path>\n");
    } else if (u_streq(line, "pid")) {
        (void)u_puts("shell pid=");
        (void)u_put_u64(ku_getpid());
        (void)u_puts(" tid=");
        (void)u_put_u64(ku_gettid());
        (void)u_puts("\n");
    } else if (u_streq(line, "hello")) {
        run_child("/apps/hello");
    } else if (u_streq(line, "external")) {
        run_child("/apps/external");
    } else if (u_streq(line, "files")) {
        run_child("/apps/files");
    } else if (u_streq(line, "monitor")) {
        run_child("/apps/monitor");
    } else if (u_streq(line, "about")) {
        run_child("/apps/about");
    } else if (u_streq(line, "exit")) {
        (void)u_puts("shell: session ended\n");
        ku_exit(0);
    } else if (u_starts_with(line, "run ")) {
        if (line[4U] == '\0') {
            (void)u_puts("usage: run /apps/name\n");
        } else {
            run_child(line + 4U);
        }
    } else if (u_starts_with(line, "echo ")) {
        (void)u_puts(line + 5U);
        (void)u_puts("\n");
    } else {
        (void)u_puts("shell: command not found\n");
    }
}

__attribute__((noreturn)) void _start(void) {
    char line[LINE_CAPACITY];
    size_t length = 0U;
    (void)u_puts("KuroganeOS userspace shell (Ring 3)\n");
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
            (void)u_puts("\n");
            line[length] = '\0';
            execute(line);
            length = 0U;
            prompt();
        } else if (character == '\b' || character == 127) {
            if (length != 0U) {
                --length;
                (void)u_puts("\b \b");
            }
        } else if (character >= 32 && character <= 126 &&
                   length + 1U < LINE_CAPACITY) {
            line[length++] = character;
            (void)u_write_all(&character, 1U);
        }
    }
}

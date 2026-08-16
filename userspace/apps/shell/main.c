#include "../../runtime/user.h"
#include "version.h"

#define LINE_CAPACITY 192U
#define HISTORY_CAPACITY 12U
#define PATH_CAPACITY 128U
#define JOB_CAPACITY 8U

static char g_history[HISTORY_CAPACITY][LINE_CAPACITY];
static size_t g_history_count = 0U;
static size_t g_history_next = 0U;
static char g_cwd[PATH_CAPACITY] = "/";
static int32_t g_last_status = 0;
static uint64_t g_jobs[JOB_CAPACITY];

static void reap_jobs(int verbose);

static int text_copy(char* destination, size_t capacity, const char* source) {
    size_t index = 0U;
    if (destination == (char*)0 || capacity == 0U || source == (const char*)0) return 0;
    while (source[index] != '\0') {
        if (index + 1U >= capacity) return 0;
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
    return 1;
}

static int text_append(char* destination, size_t capacity, const char* source) {
    size_t used = u_strlen(destination);
    size_t index = 0U;
    if (destination == (char*)0 || source == (const char*)0 || used >= capacity) return 0;
    while (source[index] != '\0') {
        if (used + index + 1U >= capacity) return 0;
        destination[used + index] = source[index];
        ++index;
    }
    destination[used + index] = '\0';
    return 1;
}

static char* trim_left(char* text) {
    while (*text == ' ' || *text == '\t') ++text;
    return text;
}

static void put_i64(int64_t value) {
    if (value < 0) {
        const uint64_t magnitude = (uint64_t)(-(value + 1)) + UINT64_C(1);
        (void)u_puts("-");
        (void)u_put_u64(magnitude);
        return;
    }
    (void)u_put_u64((uint64_t)value);
}

static void banner(void) {
    (void)u_puts("\n");
    (void)u_puts("== KUROGANE / FLUX CONSOLE =================================\n");
    (void)u_puts("   Ring 3 workspace | KuroganeOS " KUROGANE_VERSION_STRING "\n");
    (void)u_puts("   type 'help' for commands\n");
    (void)u_puts("============================================================\n");
}

static void prompt(void) {
    reap_jobs(1);
    (void)u_puts("KRG::");
    (void)u_puts(g_cwd);
    (void)u_puts(" > ");
}

static void remember_history(const char* line) {
    if (line == (const char*)0 || line[0] == '\0') return;
    if (!text_copy(g_history[g_history_next], LINE_CAPACITY, line)) return;
    g_history_next = (g_history_next + 1U) % HISTORY_CAPACITY;
    if (g_history_count < HISTORY_CAPACITY) ++g_history_count;
}

static void show_history(void) {
    size_t first = (g_history_next + HISTORY_CAPACITY - g_history_count) % HISTORY_CAPACITY;
    size_t index = 0U;
    while (index < g_history_count) {
        (void)u_put_u64(index + 1U);
        (void)u_puts("  ");
        (void)u_puts(g_history[(first + index) % HISTORY_CAPACITY]);
        (void)u_puts("\n");
        ++index;
    }
}

static int make_path(const char* input, char* output, size_t capacity) {
    if (input == (const char*)0 || input[0] == '\0') return 0;
    if (input[0] == '/') return text_copy(output, capacity, input);
    if (!text_copy(output, capacity, g_cwd)) return 0;
    if (!u_streq(output, "/") && !text_append(output, capacity, "/")) return 0;
    return text_append(output, capacity, input);
}

static int app_path(const char* name, char* output, size_t capacity) {
    if (name == (const char*)0 || name[0] == '\0') return 0;
    if (name[0] == '/') return text_copy(output, capacity, name);
    if (!text_copy(output, capacity, "/apps/")) return 0;
    return text_append(output, capacity, name);
}

static int gui_path(const char* name, char* output, size_t capacity) {
    if (name == (const char*)0 || name[0] == '\0') return 0;
    if (name[0] == '/') return text_copy(output, capacity, name);
    if (!text_copy(output, capacity, "/gui/")) return 0;
    return text_append(output, capacity, name);
}

static int run_wait(const char* path) {
    int32_t status = 0;
    if (!u_spawn_wait(path, &status)) {
        (void)u_puts("run: launch failed: ");
        (void)u_puts(path);
        (void)u_puts("\n");
        g_last_status = 127;
        return 0;
    }
    g_last_status = status;
    if (status != 0) {
        (void)u_puts("run: application exit=");
        put_i64(status);
        (void)u_puts("\n");
    }
    return status == 0;
}

static void reap_jobs(int verbose) {
    size_t index = 0U;
    while (index < JOB_CAPACITY) {
        if (g_jobs[index] != 0U) {
            int32_t status = 0;
            const ku_status_t result = ku_wait(g_jobs[index], &status);
            if (result == KU_STATUS_OK) {
                if (verbose) {
                    (void)u_puts("[job ");
                    (void)u_put_u64(g_jobs[index]);
                    (void)u_puts("] exit=");
                    put_i64(status);
                    (void)u_puts("\n");
                }
                g_jobs[index] = 0U;
            } else if (result != KU_STATUS_WOULD_BLOCK) {
                g_jobs[index] = 0U;
            }
        }
        ++index;
    }
}

static int job_slot_available(void) {
    size_t index = 0U;
    reap_jobs(0);
    while (index < JOB_CAPACITY) {
        if (g_jobs[index] == 0U) return 1;
        ++index;
    }
    return 0;
}

static int remember_job(uint64_t pid) {
    size_t index = 0U;
    reap_jobs(0);
    while (index < JOB_CAPACITY) {
        if (g_jobs[index] == 0U) {
            g_jobs[index] = pid;
            return 1;
        }
        ++index;
    }
    return 0;
}

static void list_jobs(void) {
    size_t index = 0U;
    size_t active = 0U;
    reap_jobs(1);
    while (index < JOB_CAPACITY) {
        if (g_jobs[index] != 0U) {
            (void)u_puts("job pid=");
            (void)u_put_u64(g_jobs[index]);
            (void)u_puts(" state=running\n");
            ++active;
        }
        ++index;
    }
    if (active == 0U) (void)u_puts("jobs: none\n");
    g_last_status = 0;
}

static int run_background(const char* path) {
    if (!job_slot_available()) {
        (void)u_puts("open: job table full\n");
        g_last_status = 1;
        return 0;
    }
    const ku_result_t pid = u_spawn(path);
    if (pid <= 0) {
        (void)u_puts("open: launch failed: ");
        (void)u_puts(path);
        (void)u_puts("\n");
        g_last_status = 127;
        return 0;
    }
    if (!remember_job((uint64_t)pid)) {
        (void)u_puts("open: internal job tracking failure\n");
        g_last_status = 1;
        return 0;
    }
    (void)u_puts("open: pid=");
    (void)u_put_u64((uint64_t)pid);
    (void)u_puts(" path=");
    (void)u_puts(path);
    (void)u_puts("\n");
    g_last_status = 0;
    return 1;
}

static void list_apps(void) {
    (void)u_puts("console apps:\n");
    (void)u_puts("  shell hello external files monitor about\n");
    (void)u_puts("desktop surfaces:\n");
    (void)u_puts("  terminal files sysmon settings about\n");
    (void)u_puts("developer apps:\n");
    (void)u_puts("  run <name>        -> /apps/<name>\n");
    (void)u_puts("  run /path         -> exact ELF path\n");
    (void)u_puts("  gui <name>        -> /gui/<name> in background\n");
}

static void show_help(void) {
    (void)u_puts("Kurogane Flux shell commands\n\n");
    (void)u_puts("workspace:\n");
    (void)u_puts("  help clear version uname pid whoami status history jobs\n");
    (void)u_puts("  pwd cd <path> cat <path> read <path> which <name>\n");
    (void)u_puts("execution:\n");
    (void)u_puts("  apps run <name|/path> open <name|/path> gui <name> jobs wait <pid>\n");
    (void)u_puts("  hello external files monitor about\n");
    (void)u_puts("utility:\n");
    (void)u_puts("  echo <text> calc <a> <+|-|*|/|%> <b> sleep <ticks> yield\n");
    (void)u_puts("  true false exit\n");
    (void)u_puts("diagnostic shortcuts:\n");
    (void)u_puts("  mem free tasks pci device driver diskinfo -> gui sysmon\n");
    (void)u_puts("\n");
    (void)u_puts("Low-level net/fs mutation/reboot diagnostics remain in the\n");
    (void)u_puts("kernel developer console until dedicated capability syscalls exist.\n");
}

static int parse_i64(const char* text, int64_t* output) {
    uint64_t value = 0U;
    int negative = 0;
    size_t index = 0U;
    if (text == (const char*)0 || output == (int64_t*)0 || text[0] == '\0') return 0;
    if (text[index] == '-' || text[index] == '+') {
        negative = text[index] == '-';
        ++index;
    }
    if (text[index] < '0' || text[index] > '9') return 0;
    while (text[index] >= '0' && text[index] <= '9') {
        const uint64_t digit = (uint64_t)(text[index] - '0');
        if (value > (UINT64_MAX - digit) / UINT64_C(10)) return 0;
        value = value * UINT64_C(10) + digit;
        ++index;
    }
    if (text[index] != '\0') return 0;
    if (!negative) {
        if (value > (uint64_t)INT64_MAX) return 0;
        *output = (int64_t)value;
    } else {
        const uint64_t limit = (uint64_t)INT64_MAX + UINT64_C(1);
        if (value > limit) return 0;
        *output = value == limit ? INT64_MIN : -(int64_t)value;
    }
    return 1;
}

static void command_calc(char* arguments) {
    char* left_text = trim_left(arguments);
    char* cursor = left_text;
    char* operator_text;
    char* right_text;
    int64_t left;
    int64_t right;
    int64_t result;

    while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') ++cursor;
    if (*cursor == '\0') goto usage;
    *cursor++ = '\0';
    operator_text = trim_left(cursor);
    cursor = operator_text;
    while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') ++cursor;
    if (*cursor == '\0') goto usage;
    *cursor++ = '\0';
    right_text = trim_left(cursor);
    if (right_text[0] == '\0' || operator_text[0] == '\0' || operator_text[1] != '\0') goto usage;
    if (!parse_i64(left_text, &left) || !parse_i64(right_text, &right)) goto usage;

    switch (operator_text[0]) {
    case '+':
        if ((right > 0 && left > INT64_MAX - right) ||
            (right < 0 && left < INT64_MIN - right)) {
            (void)u_puts("calc: integer overflow\n");
            g_last_status = 2;
            return;
        }
        result = left + right;
        break;
    case '-':
        if ((right < 0 && left > INT64_MAX + right) ||
            (right > 0 && left < INT64_MIN + right)) {
            (void)u_puts("calc: integer overflow\n");
            g_last_status = 2;
            return;
        }
        result = left - right;
        break;
    case '*':
        if (left != 0 && right != 0) {
            if (left == -1 && right == INT64_MIN) {
                (void)u_puts("calc: integer overflow\n");
                g_last_status = 2;
                return;
            }
            if (right == -1 && left == INT64_MIN) {
                (void)u_puts("calc: integer overflow\n");
                g_last_status = 2;
                return;
            }
            if ((left > 0 && right > 0 && left > INT64_MAX / right) ||
                (left > 0 && right < 0 && right < INT64_MIN / left) ||
                (left < 0 && right > 0 && left < INT64_MIN / right) ||
                (left < 0 && right < 0 && left < INT64_MAX / right)) {
                (void)u_puts("calc: integer overflow\n");
                g_last_status = 2;
                return;
            }
        }
        result = left * right;
        break;
    case '/':
        if (right == 0) {
            (void)u_puts("calc: division by zero\n");
            g_last_status = 2;
            return;
        }
        if (left == INT64_MIN && right == -1) {
            (void)u_puts("calc: integer overflow\n");
            g_last_status = 2;
            return;
        }
        result = left / right;
        break;
    case '%':
        if (right == 0) {
            (void)u_puts("calc: division by zero\n");
            g_last_status = 2;
            return;
        }
        result = (left == INT64_MIN && right == -1) ? 0 : left % right;
        break;
    default:
        goto usage;
    }
    put_i64(result);
    (void)u_puts("\n");
    g_last_status = 0;
    return;

usage:
    (void)u_puts("usage: calc <a> <+|-|*|/|%> <b>\n");
    g_last_status = 2;
}

static void command_cat(const char* raw_path) {
    char path[PATH_CAPACITY];
    char buffer[256];
    if (!make_path(raw_path, path, sizeof(path))) {
        (void)u_puts("cat: invalid path\n");
        g_last_status = 2;
        return;
    }
    const ku_result_t handle = ku_open(path, u_strlen(path), KU_OPEN_READ);
    if (handle <= 0) {
        (void)u_puts("cat: cannot open ");
        (void)u_puts(path);
        (void)u_puts("\n");
        g_last_status = 1;
        return;
    }
    for (;;) {
        const ku_result_t count = ku_read((ku_handle_t)handle, buffer, sizeof(buffer));
        if (count == 0) break;
        if (count < 0) {
            (void)u_puts("\ncat: read failed\n");
            g_last_status = 1;
            (void)ku_close((ku_handle_t)handle);
            return;
        }
        if (!u_write_all(buffer, (size_t)count)) {
            g_last_status = 1;
            (void)ku_close((ku_handle_t)handle);
            return;
        }
    }
    (void)ku_close((ku_handle_t)handle);
    (void)u_puts("\n");
    g_last_status = 0;
}

static void command_cd(const char* path) {
    if (path == (const char*)0 || path[0] == '\0' || u_streq(path, "/")) {
        (void)text_copy(g_cwd, sizeof(g_cwd), "/");
        g_last_status = 0;
        return;
    }
    if (u_streq(path, ".")) {
        g_last_status = 0;
        return;
    }
    if (u_streq(path, "..")) {
        size_t length = u_strlen(g_cwd);
        if (length <= 1U) {
            (void)text_copy(g_cwd, sizeof(g_cwd), "/");
        } else {
            while (length > 1U && g_cwd[length - 1U] != '/') --length;
            if (length <= 1U) {
                (void)text_copy(g_cwd, sizeof(g_cwd), "/");
            } else {
                g_cwd[length - 1U] = '\0';
            }
        }
        g_last_status = 0;
        return;
    }
    if (path[0] == '/') {
        if (!text_copy(g_cwd, sizeof(g_cwd), path)) {
            (void)u_puts("cd: path too long\n");
            g_last_status = 2;
            return;
        }
    } else {
        char resolved[PATH_CAPACITY];
        if (!make_path(path, resolved, sizeof(resolved)) ||
            !text_copy(g_cwd, sizeof(g_cwd), resolved)) {
            (void)u_puts("cd: path too long\n");
            g_last_status = 2;
            return;
        }
    }
    g_last_status = 0;
}

static void diagnostics_shortcut(void) {
    (void)u_puts("diag: system-info capability is limited; opening Ring-3 monitor surface\n");
    (void)run_background("/gui/sysmon");
}

static void execute(char* line) {
    char* command;
    char* arguments;
    char* separator;

    line = trim_left(line);
    if (line[0] == '\0') return;
    remember_history(line);

    command = line;
    separator = command;
    while (*separator != '\0' && *separator != ' ' && *separator != '\t') ++separator;
    if (*separator != '\0') {
        *separator++ = '\0';
        arguments = trim_left(separator);
    } else {
        arguments = separator;
    }

    if (u_streq(command, "help")) {
        show_help();
        g_last_status = 0;
    } else if (u_streq(command, "clear")) {
        size_t index = 0U;
        while (index++ < 32U) (void)u_puts("\n");
        banner();
        g_last_status = 0;
    } else if (u_streq(command, "version") || u_streq(command, "uname")) {
        (void)u_puts(KUROGANE_PRODUCT_STRING " x86_64 UEFI / Ring 3\n");
        g_last_status = 0;
    } else if (u_streq(command, "pid")) {
        (void)u_puts("pid=");
        (void)u_put_u64(ku_getpid());
        (void)u_puts(" tid=");
        (void)u_put_u64(ku_gettid());
        (void)u_puts("\n");
        g_last_status = 0;
    } else if (u_streq(command, "whoami")) {
        (void)u_puts("user\n");
        g_last_status = 0;
    } else if (u_streq(command, "status")) {
        (void)u_puts("last_status=");
        put_i64(g_last_status);
        (void)u_puts("\n");
    } else if (u_streq(command, "history")) {
        show_history();
        g_last_status = 0;
    } else if (u_streq(command, "jobs")) {
        list_jobs();
    } else if (u_streq(command, "wait")) {
        int64_t pid = 0;
        int32_t status = 0;
        if (!parse_i64(arguments, &pid) || pid <= 0) {
            (void)u_puts("usage: wait <pid>\n");
            g_last_status = 2;
        } else if (!u_wait((uint64_t)pid, &status)) {
            (void)u_puts("wait: process is not a waitable child\n");
            g_last_status = 1;
        } else {
            size_t index = 0U;
            while (index < JOB_CAPACITY) {
                if (g_jobs[index] == (uint64_t)pid) g_jobs[index] = 0U;
                ++index;
            }
            g_last_status = status;
            (void)u_puts("wait: exit=");
            put_i64(status);
            (void)u_puts("\n");
        }
    } else if (u_streq(command, "pwd")) {
        (void)u_puts(g_cwd);
        (void)u_puts("\n");
        g_last_status = 0;
    } else if (u_streq(command, "cd")) {
        command_cd(arguments);
    } else if (u_streq(command, "cat") || u_streq(command, "read")) {
        if (arguments[0] == '\0') {
            (void)u_puts("usage: cat <path>\n");
            g_last_status = 2;
        } else {
            command_cat(arguments);
        }
    } else if (u_streq(command, "apps")) {
        list_apps();
        g_last_status = 0;
    } else if (u_streq(command, "run")) {
        char path[PATH_CAPACITY];
        if (!app_path(arguments, path, sizeof(path))) {
            (void)u_puts("usage: run <name|/path>\n");
            g_last_status = 2;
        } else {
            (void)run_wait(path);
        }
    } else if (u_streq(command, "open")) {
        char path[PATH_CAPACITY];
        if (!app_path(arguments, path, sizeof(path))) {
            (void)u_puts("usage: open <name|/path>\n");
            g_last_status = 2;
        } else {
            (void)run_background(path);
        }
    } else if (u_streq(command, "gui")) {
        char path[PATH_CAPACITY];
        if (!gui_path(arguments, path, sizeof(path))) {
            (void)u_puts("usage: gui <terminal|files|sysmon|settings|about>\n");
            g_last_status = 2;
        } else {
            (void)run_background(path);
        }
    } else if (u_streq(command, "which")) {
        char path[PATH_CAPACITY];
        if (!app_path(arguments, path, sizeof(path))) {
            (void)u_puts("usage: which <name>\n");
            g_last_status = 2;
        } else {
            (void)u_puts(path);
            (void)u_puts("\n");
            g_last_status = 0;
        }
    } else if (u_streq(command, "hello") || u_streq(command, "external") ||
               u_streq(command, "files") || u_streq(command, "monitor") ||
               u_streq(command, "about")) {
        char path[PATH_CAPACITY];
        if (app_path(command, path, sizeof(path))) (void)run_wait(path);
    } else if (u_streq(command, "mem") || u_streq(command, "free") ||
               u_streq(command, "tasks") || u_streq(command, "pci") ||
               u_streq(command, "device") || u_streq(command, "driver") ||
               u_streq(command, "diskinfo")) {
        diagnostics_shortcut();
    } else if (u_streq(command, "echo")) {
        (void)u_puts(arguments);
        (void)u_puts("\n");
        g_last_status = 0;
    } else if (u_streq(command, "calc")) {
        command_calc(arguments);
    } else if (u_streq(command, "sleep")) {
        int64_t ticks = 0;
        if (!parse_i64(arguments, &ticks) || ticks < 0) {
            (void)u_puts("usage: sleep <ticks>\n");
            g_last_status = 2;
        } else {
            g_last_status = ku_sleep((uint64_t)ticks) == KU_STATUS_OK ? 0 : 1;
        }
    } else if (u_streq(command, "yield")) {
        g_last_status = ku_yield() == KU_STATUS_OK ? 0 : 1;
    } else if (u_streq(command, "true")) {
        g_last_status = 0;
    } else if (u_streq(command, "false")) {
        g_last_status = 1;
    } else if (u_streq(command, "exit")) {
        (void)u_puts("Flux console session ended\n");
        ku_exit(g_last_status);
    } else if (u_streq(command, "net") || u_streq(command, "ip") ||
               u_streq(command, "ifconfig") || u_streq(command, "route") ||
               u_streq(command, "arp") || u_streq(command, "ping") ||
               u_streq(command, "nslookup") || u_streq(command, "date") ||
               u_streq(command, "uptime") || u_streq(command, "ls") ||
               u_streq(command, "stat") || u_streq(command, "touch") ||
               u_streq(command, "mkdir") || u_streq(command, "rmdir") ||
               u_streq(command, "write") || u_streq(command, "cp") ||
               u_streq(command, "mv") || u_streq(command, "rm") ||
               u_streq(command, "reboot") || u_streq(command, "poweroff") ||
               u_streq(command, "shutdown")) {
        (void)u_puts("shell: command belongs to the privileged developer console;\n");
        (void)u_puts("       dedicated Ring-3 capability syscall not available yet.\n");
        g_last_status = 126;
    } else {
        (void)u_puts("shell: command not found: ");
        (void)u_puts(command);
        (void)u_puts("\n");
        g_last_status = 127;
    }
}

__attribute__((noreturn)) void _start(void) {
    char line[LINE_CAPACITY];
    size_t length = 0U;
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
        } else if (character == 0x15) {
            while (length != 0U) {
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

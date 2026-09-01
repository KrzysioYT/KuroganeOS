#ifndef KUROGANE_TERMINAL_SHELL_H
#define KUROGANE_TERMINAL_SHELL_H

#include "shell_core.h"

/*
 * Stable, desktop-neutral frontend API for KuroganeOS terminal applications.
 *
 * shell_core.h predates the userspace/desktop split and still carries legacy
 * flux_shell_* implementation identifiers internally. Frontends must not
 * depend on those identifiers. This boundary also adds the normal KuroganeOS
 * /apps command lookup used by Anvil-installed utilities.
 */
typedef flux_shell_emit_fn ku_shell_emit_fn;
typedef flux_shell_io ku_shell_io;
typedef flux_shell_action ku_shell_action;
typedef flux_shell_state ku_shell_state;

#define KU_SHELL_LINE_CAPACITY FLUX_SHELL_LINE_CAPACITY
#define KU_SHELL_ACTION_NONE FLUX_SHELL_ACTION_NONE
#define KU_SHELL_ACTION_CLEAR FLUX_SHELL_ACTION_CLEAR
#define KU_SHELL_ACTION_EXIT FLUX_SHELL_ACTION_EXIT

static inline void ku_shell_initialize(ku_shell_state* state) {
    flux_shell_initialize(state);
}

static inline void ku_shell_reap_jobs(
    ku_shell_state* state,
    const ku_shell_io* io,
    int verbose) {
    flux_shell_reap_jobs(state, io, verbose);
}

static inline int ku_shell_history_previous(
    ku_shell_state* state,
    char* output,
    size_t capacity) {
    return flux_shell_history_previous(state, output, capacity);
}

static inline int ku_shell_history_next(
    ku_shell_state* state,
    char* output,
    size_t capacity) {
    return flux_shell_history_next(state, output, capacity);
}

static inline int ku_shell_reserved_command(const char* command) {
    static const char* const reserved[] = {
        "help", "clear", "version", "uname", "pid", "whoami", "status",
        "history", "jobs", "wait", "pwd", "cd", "cat", "read", "which",
        "apps", "run", "open", "hello", "external", "files", "monitor",
        "about", "echo", "calc", "sleep", "yield", "true", "false", "exit",
        "net", "ip", "ifconfig", "route", "arp", "ping", "nslookup",
        "date", "uptime", "ls", "stat", "touch", "mkdir", "rmdir", "write",
        "cp", "mv", "rm", "reboot", "poweroff", "shutdown", "mem", "free",
        "tasks", "pci", "device", "driver", "diskinfo"
    };
    size_t index;
    for (index = 0U; index < sizeof(reserved) / sizeof(reserved[0]); ++index) {
        if (flux_shell_streq(command, reserved[index])) return 1;
    }
    return 0;
}

static inline int ku_shell_try_installed_app(
    ku_shell_state* state,
    const ku_shell_io* io,
    const char* input_line) {
    char command[FLUX_SHELL_LINE_CAPACITY];
    char path[FLUX_SHELL_PATH_CAPACITY];
    size_t length = 0U;

    if (state == (ku_shell_state*)0 || input_line == (const char*)0) return 0;
    while (input_line[length] == ' ' || input_line[length] == '\t') ++length;
    input_line += length;
    length = 0U;
    while (input_line[length] != '\0' &&
           input_line[length] != ' ' && input_line[length] != '\t') {
        if (length + 1U >= sizeof(command)) return 0;
        command[length] = input_line[length];
        ++length;
    }
    command[length] = '\0';
    if (length == 0U || input_line[length] != '\0' || command[0] == '/' ||
        ku_shell_reserved_command(command)) return 0;
    if (!flux_shell_root_path("/apps/", command, path, sizeof(path)) ||
        !flux_shell_file_exists(path)) return 0;

    flux_shell_remember_history(state, input_line);
    (void)flux_shell_run_wait(state, io, path);
    return 1;
}

static inline ku_shell_action ku_shell_execute(
    ku_shell_state* state,
    const ku_shell_io* io,
    const char* input_line) {
    if (ku_shell_try_installed_app(state, io, input_line)) {
        return KU_SHELL_ACTION_NONE;
    }
    return flux_shell_execute(state, io, input_line);
}

#endif

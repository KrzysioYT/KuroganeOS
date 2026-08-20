#ifndef KUROGANE_TERMINAL_SHELL_H
#define KUROGANE_TERMINAL_SHELL_H

#include "flux_shell.h"

/*
 * Public terminal policy layer.
 *
 * Red Flux window/session control belongs to the desktop/session manager, not
 * to an interactive shell command namespace.  The historical shared shell
 * core still contains compatibility helpers used by older code, but terminal
 * frontends must go through this policy wrapper so commands such as `gui`,
 * `home`, implicit `/gui/*` resolution, and diagnostic GUI launch shortcuts
 * are not exposed to users.
 */

static inline int terminal_shell_gui_path(const char* path) {
    return path != (const char*)0 &&
        (flux_shell_streq(path, "/gui") ||
         flux_shell_starts_with(path, "/gui/"));
}

static inline int terminal_shell_diag_command(const char* command) {
    return flux_shell_streq(command, "mem") ||
        flux_shell_streq(command, "free") ||
        flux_shell_streq(command, "tasks") ||
        flux_shell_streq(command, "pci") ||
        flux_shell_streq(command, "device") ||
        flux_shell_streq(command, "driver") ||
        flux_shell_streq(command, "diskinfo");
}

static inline void terminal_shell_help(const flux_shell_io* io) {
    flux_shell_emit(io, "workspace: help clear version uname pid whoami status");
    flux_shell_emit(io, "history jobs wait pwd cd cat read which apps");
    flux_shell_emit(io, "execute: run <app> open <app|/apps/path>");
    flux_shell_emit(io, "utility: echo calc sleep yield true false exit");
    flux_shell_emit(io, "Red Flux windows are managed by the desktop, not the terminal");
}

static inline void terminal_shell_apps(const flux_shell_io* io) {
    flux_shell_emit(io, "apps: shell hello external files monitor about");
}

static inline void terminal_shell_which(
    flux_shell_state* state,
    const flux_shell_io* io,
    const char* name) {
    char path[FLUX_SHELL_PATH_CAPACITY];

    if (name == (const char*)0 || name[0] == '\0') {
        flux_shell_emit(io, "usage: which <app|/apps/path>");
        state->last_status = 2;
        return;
    }

    if (terminal_shell_gui_path(name)) {
        flux_shell_emit(io, "which: GUI surfaces are not terminal commands");
        state->last_status = 1;
        return;
    }

    if (name[0] == '/') {
        if (flux_shell_file_exists(name)) {
            flux_shell_emit(io, name);
            state->last_status = 0;
        } else {
            flux_shell_emit(io, "which: not found");
            state->last_status = 1;
        }
        return;
    }

    if (flux_shell_root_path("/apps/", name, path, sizeof(path)) &&
        flux_shell_file_exists(path)) {
        flux_shell_emit(io, path);
        state->last_status = 0;
        return;
    }

    flux_shell_emit(io, "which: not found");
    state->last_status = 1;
}

static inline void terminal_shell_open(
    flux_shell_state* state,
    const flux_shell_io* io,
    const char* arguments) {
    char path[FLUX_SHELL_PATH_CAPACITY];

    if (arguments == (const char*)0 || arguments[0] == '\0') {
        flux_shell_emit(io, "usage: open <app|/apps/path>");
        state->last_status = 2;
        return;
    }

    if (terminal_shell_gui_path(arguments)) {
        flux_shell_emit(io, "open: Red Flux surfaces are controlled by the desktop");
        state->last_status = 126;
        return;
    }

    if (arguments[0] == '/') {
        (void)flux_shell_run_background(state, io, arguments);
        return;
    }

    if (!flux_shell_root_path("/apps/", arguments, path, sizeof(path))) {
        flux_shell_emit(io, "open: invalid app name");
        state->last_status = 2;
        return;
    }
    (void)flux_shell_run_background(state, io, path);
}

static inline void terminal_shell_removed_gui_command(
    flux_shell_state* state,
    const flux_shell_io* io) {
    flux_shell_emit(io,
        "command removed: Red Flux UI is controlled by the desktop/session manager");
    state->last_status = 126;
}

static inline void terminal_shell_removed_diag_shortcut(
    flux_shell_state* state,
    const flux_shell_io* io) {
    flux_shell_emit(io,
        "diagnostic GUI shortcut removed; use a dedicated Ring-3 diagnostic capability");
    state->last_status = 126;
}

static inline flux_shell_action terminal_shell_execute(
    flux_shell_state* state,
    const flux_shell_io* io,
    const char* input_line) {
    char storage[FLUX_SHELL_LINE_CAPACITY];
    char* line;
    char* command;
    char* arguments;
    char* separator;

    if (state == (flux_shell_state*)0 || input_line == (const char*)0) {
        return FLUX_SHELL_ACTION_NONE;
    }
    if (!flux_shell_copy(storage, sizeof(storage), input_line)) {
        flux_shell_emit(io, "shell: input too long");
        state->last_status = 2;
        return FLUX_SHELL_ACTION_NONE;
    }

    line = flux_shell_trim_left(storage);
    if (line[0] == '\0') return FLUX_SHELL_ACTION_NONE;

    command = line;
    separator = command;
    while (*separator != '\0' && *separator != ' ' && *separator != '\t') ++separator;
    if (*separator != '\0') {
        *separator++ = '\0';
        arguments = flux_shell_trim_left(separator);
    } else {
        arguments = separator;
    }

    if (flux_shell_streq(command, "help") ||
        flux_shell_streq(command, "apps") ||
        flux_shell_streq(command, "which") ||
        flux_shell_streq(command, "open") ||
        flux_shell_streq(command, "gui") ||
        flux_shell_streq(command, "home") ||
        terminal_shell_diag_command(command)) {
        flux_shell_remember_history(state, line);

        if (flux_shell_streq(command, "help")) {
            terminal_shell_help(io);
            state->last_status = 0;
        } else if (flux_shell_streq(command, "apps")) {
            terminal_shell_apps(io);
            state->last_status = 0;
        } else if (flux_shell_streq(command, "which")) {
            terminal_shell_which(state, io, arguments);
        } else if (flux_shell_streq(command, "open")) {
            terminal_shell_open(state, io, arguments);
        } else if (flux_shell_streq(command, "gui") ||
                   flux_shell_streq(command, "home")) {
            terminal_shell_removed_gui_command(state, io);
        } else {
            terminal_shell_removed_diag_shortcut(state, io);
        }
        return FLUX_SHELL_ACTION_NONE;
    }

    return flux_shell_execute(state, io, input_line);
}

#endif

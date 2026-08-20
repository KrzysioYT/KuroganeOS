#ifndef KUROGANE_TERMINAL_SHELL_H
#define KUROGANE_TERMINAL_SHELL_H

#include "shell_core.h"

/*
 * Stable, desktop-neutral frontend API for KuroganeOS terminal applications.
 *
 * shell_core.h predates the userspace/desktop split and still carries legacy
 * flux_shell_* implementation identifiers internally.  Frontends must not
 * depend on those identifiers.  This boundary keeps the terminal/shell ABI
 * independent from Red Flux so the implementation can be renamed or replaced
 * without touching console and graphical terminal applications again.
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

static inline ku_shell_action ku_shell_execute(
    ku_shell_state* state,
    const ku_shell_io* io,
    const char* input_line) {
    return flux_shell_execute(state, io, input_line);
}

#endif

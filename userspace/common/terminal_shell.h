#ifndef KUROGANE_TERMINAL_SHELL_H
#define KUROGANE_TERMINAL_SHELL_H

#include "shell_core.h"

/*
 * Stable frontend entry point for KuroganeOS terminal applications.
 *
 * The command core is deliberately desktop-agnostic.  Red Flux window/session
 * management belongs to the desktop/session manager and is not represented by
 * hidden terminal commands, path fallbacks, or compatibility shortcuts here.
 */
static inline flux_shell_action terminal_shell_execute(
    flux_shell_state* state,
    const flux_shell_io* io,
    const char* input_line) {
    return flux_shell_execute(state, io, input_line);
}

#endif

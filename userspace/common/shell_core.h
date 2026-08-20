#ifndef KUROGANE_SHELL_CORE_H
#define KUROGANE_SHELL_CORE_H

#include "../runtime/user.h"
#include "../../common/version.h"

/*
 * Generic Ring-3 command shell core.
 *
 * This layer intentionally knows nothing about Red Flux windows, launchers,
 * desktop surfaces or diagnostic GUI applications.  It owns only command
 * parsing, history, cwd bookkeeping, child jobs and generic program/file
 * operations exposed by the userspace ABI.
 *
 * The flux_shell_* names are kept temporarily as ABI/source compatibility for
 * the two existing frontends.  New desktop policy must not be added here.
 */

#define FLUX_SHELL_LINE_CAPACITY 192U
#define FLUX_SHELL_HISTORY_CAPACITY 12U
#define FLUX_SHELL_PATH_CAPACITY 128U
#define FLUX_SHELL_JOB_CAPACITY 8U
#define FLUX_SHELL_OUTPUT_CAPACITY 128U

typedef void (*flux_shell_emit_fn)(void* context, const char* line);

typedef struct flux_shell_io {
    flux_shell_emit_fn emit;
    void* context;
} flux_shell_io;

typedef enum flux_shell_action {
    FLUX_SHELL_ACTION_NONE = 0,
    FLUX_SHELL_ACTION_CLEAR = 1,
    FLUX_SHELL_ACTION_EXIT = 2
} flux_shell_action;

typedef struct flux_shell_state {
    char history[FLUX_SHELL_HISTORY_CAPACITY][FLUX_SHELL_LINE_CAPACITY];
    size_t history_count;
    size_t history_next;
    size_t history_cursor;
    char cwd[FLUX_SHELL_PATH_CAPACITY];
    int32_t last_status;
    uint64_t jobs[FLUX_SHELL_JOB_CAPACITY];
} flux_shell_state;

static inline size_t flux_shell_strlen(const char* text) {
    size_t length = 0U;
    if (text == (const char*)0) return 0U;
    while (text[length] != '\0') ++length;
    return length;
}

static inline int flux_shell_streq(const char* left, const char* right) {
    size_t index = 0U;
    if (left == (const char*)0 || right == (const char*)0) return 0;
    while (left[index] != '\0' && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

static inline int flux_shell_starts_with(const char* text, const char* prefix) {
    size_t index = 0U;
    if (text == (const char*)0 || prefix == (const char*)0) return 0;
    while (prefix[index] != '\0') {
        if (text[index] != prefix[index]) return 0;
        ++index;
    }
    return 1;
}

static inline int flux_shell_copy(
    char* destination, size_t capacity, const char* source) {
    size_t index = 0U;
    if (destination == (char*)0 || source == (const char*)0 || capacity == 0U) return 0;
    while (source[index] != '\0') {
        if (index + 1U >= capacity) return 0;
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
    return 1;
}

static inline int flux_shell_append(
    char* destination, size_t capacity, const char* source) {
    const size_t used = flux_shell_strlen(destination);
    size_t index = 0U;
    if (destination == (char*)0 || source == (const char*)0 ||
        capacity == 0U || used >= capacity) return 0;
    while (source[index] != '\0') {
        if (used + index + 1U >= capacity) return 0;
        destination[used + index] = source[index];
        ++index;
    }
    destination[used + index] = '\0';
    return 1;
}

static inline char* flux_shell_trim_left(char* text) {
    while (*text == ' ' || *text == '\t') ++text;
    return text;
}

static inline void flux_shell_emit(const flux_shell_io* io, const char* line) {
    if (io != (const flux_shell_io*)0 && io->emit != (flux_shell_emit_fn)0) {
        io->emit(io->context, line != (const char*)0 ? line : "");
    }
}

static inline void flux_shell_format_u64(
    char* output, size_t capacity, uint64_t value) {
    char reverse[24];
    size_t count = 0U;
    size_t written = 0U;
    if (capacity == 0U) return;
    do {
        reverse[count++] = (char)('0' + (value % UINT64_C(10)));
        value /= UINT64_C(10);
    } while (value != 0U && count < sizeof(reverse));
    while (count != 0U && written + 1U < capacity) {
        output[written++] = reverse[--count];
    }
    output[written] = '\0';
}

static inline void flux_shell_format_i64(
    char* output, size_t capacity, int64_t value) {
    uint64_t magnitude;
    if (capacity == 0U) return;
    if (value >= 0) {
        flux_shell_format_u64(output, capacity, (uint64_t)value);
        return;
    }
    if (capacity < 2U) {
        output[0] = '\0';
        return;
    }
    output[0] = '-';
    magnitude = (uint64_t)(-(value + 1)) + UINT64_C(1);
    flux_shell_format_u64(output + 1U, capacity - 1U, magnitude);
}

static inline void flux_shell_emit_number(
    const flux_shell_io* io,
    const char* prefix,
    int64_t value,
    const char* suffix) {
    char line[FLUX_SHELL_OUTPUT_CAPACITY];
    char number[24];
    line[0] = '\0';
    flux_shell_format_i64(number, sizeof(number), value);
    (void)flux_shell_copy(line, sizeof(line), prefix != (const char*)0 ? prefix : "");
    (void)flux_shell_append(line, sizeof(line), number);
    if (suffix != (const char*)0) (void)flux_shell_append(line, sizeof(line), suffix);
    flux_shell_emit(io, line);
}

static inline int flux_shell_parse_i64(const char* text, int64_t* output) {
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

static inline void flux_shell_initialize(flux_shell_state* state) {
    size_t outer;
    size_t inner;
    if (state == (flux_shell_state*)0) return;
    for (outer = 0U; outer < FLUX_SHELL_HISTORY_CAPACITY; ++outer) {
        for (inner = 0U; inner < FLUX_SHELL_LINE_CAPACITY; ++inner) {
            state->history[outer][inner] = '\0';
        }
    }
    for (outer = 0U; outer < FLUX_SHELL_JOB_CAPACITY; ++outer) {
        state->jobs[outer] = 0U;
    }
    state->history_count = 0U;
    state->history_next = 0U;
    state->history_cursor = 0U;
    state->last_status = 0;
    (void)flux_shell_copy(state->cwd, sizeof(state->cwd), "/");
}

static inline void flux_shell_remember_history(
    flux_shell_state* state, const char* line) {
    if (state == (flux_shell_state*)0 || line == (const char*)0 || line[0] == '\0') return;
    if (!flux_shell_copy(
            state->history[state->history_next],
            FLUX_SHELL_LINE_CAPACITY,
            line)) return;
    state->history_next =
        (state->history_next + 1U) % FLUX_SHELL_HISTORY_CAPACITY;
    if (state->history_count < FLUX_SHELL_HISTORY_CAPACITY) ++state->history_count;
    state->history_cursor = state->history_count;
}

static inline size_t flux_shell_history_physical(
    const flux_shell_state* state, size_t logical) {
    const size_t first =
        (state->history_next + FLUX_SHELL_HISTORY_CAPACITY - state->history_count) %
        FLUX_SHELL_HISTORY_CAPACITY;
    return (first + logical) % FLUX_SHELL_HISTORY_CAPACITY;
}

static inline int flux_shell_history_previous(
    flux_shell_state* state, char* output, size_t capacity) {
    if (state == (flux_shell_state*)0 || output == (char*)0 || capacity == 0U ||
        state->history_count == 0U) return 0;
    if (state->history_cursor > state->history_count) {
        state->history_cursor = state->history_count;
    }
    if (state->history_cursor != 0U) --state->history_cursor;
    return flux_shell_copy(
        output, capacity,
        state->history[flux_shell_history_physical(state, state->history_cursor)]);
}

static inline int flux_shell_history_next(
    flux_shell_state* state, char* output, size_t capacity) {
    if (state == (flux_shell_state*)0 || output == (char*)0 || capacity == 0U) return 0;
    if (state->history_count == 0U) {
        output[0] = '\0';
        return 0;
    }
    if (state->history_cursor < state->history_count) ++state->history_cursor;
    if (state->history_cursor >= state->history_count) {
        state->history_cursor = state->history_count;
        output[0] = '\0';
        return 1;
    }
    return flux_shell_copy(
        output, capacity,
        state->history[flux_shell_history_physical(state, state->history_cursor)]);
}

static inline void flux_shell_show_history(
    const flux_shell_state* state, const flux_shell_io* io) {
    size_t logical;
    if (state == (const flux_shell_state*)0) return;
    for (logical = 0U; logical < state->history_count; ++logical) {
        char line[FLUX_SHELL_OUTPUT_CAPACITY];
        char number[24];
        line[0] = '\0';
        flux_shell_format_u64(number, sizeof(number), logical + 1U);
        (void)flux_shell_copy(line, sizeof(line), number);
        (void)flux_shell_append(line, sizeof(line), "  ");
        (void)flux_shell_append(
            line, sizeof(line),
            state->history[flux_shell_history_physical(state, logical)]);
        flux_shell_emit(io, line);
    }
}

static inline int flux_shell_make_path(
    const flux_shell_state* state,
    const char* input,
    char* output,
    size_t capacity) {
    if (state == (const flux_shell_state*)0 || input == (const char*)0 || input[0] == '\0') return 0;
    if (input[0] == '/') return flux_shell_copy(output, capacity, input);
    if (!flux_shell_copy(output, capacity, state->cwd)) return 0;
    if (!flux_shell_streq(output, "/") && !flux_shell_append(output, capacity, "/")) return 0;
    return flux_shell_append(output, capacity, input);
}

static inline int flux_shell_root_path(
    const char* root,
    const char* name,
    char* output,
    size_t capacity) {
    if (name == (const char*)0 || name[0] == '\0') return 0;
    if (name[0] == '/') return flux_shell_copy(output, capacity, name);
    if (!flux_shell_copy(output, capacity, root)) return 0;
    return flux_shell_append(output, capacity, name);
}

static inline int flux_shell_file_exists(const char* path) {
    const ku_result_t handle = ku_open(path, flux_shell_strlen(path), KU_OPEN_READ);
    if (handle <= 0) return 0;
    (void)ku_close((ku_handle_t)handle);
    return 1;
}

static inline void flux_shell_reap_jobs(
    flux_shell_state* state, const flux_shell_io* io, int verbose) {
    size_t index;
    if (state == (flux_shell_state*)0) return;
    for (index = 0U; index < FLUX_SHELL_JOB_CAPACITY; ++index) {
        if (state->jobs[index] != 0U) {
            int32_t status = 0;
            const ku_status_t result = ku_wait(state->jobs[index], &status);
            if (result == KU_STATUS_OK) {
                if (verbose) {
                    char line[FLUX_SHELL_OUTPUT_CAPACITY] = "job ";
                    char number[24];
                    flux_shell_format_u64(number, sizeof(number), state->jobs[index]);
                    (void)flux_shell_append(line, sizeof(line), number);
                    (void)flux_shell_append(line, sizeof(line), " exit=");
                    flux_shell_format_i64(number, sizeof(number), status);
                    (void)flux_shell_append(line, sizeof(line), number);
                    flux_shell_emit(io, line);
                }
                state->jobs[index] = 0U;
            } else if (result != KU_STATUS_WOULD_BLOCK) {
                state->jobs[index] = 0U;
            }
        }
    }
}

static inline int flux_shell_remember_job(flux_shell_state* state, uint64_t pid) {
    size_t index;
    flux_shell_reap_jobs(state, (const flux_shell_io*)0, 0);
    for (index = 0U; index < FLUX_SHELL_JOB_CAPACITY; ++index) {
        if (state->jobs[index] == 0U) {
            state->jobs[index] = pid;
            return 1;
        }
    }
    return 0;
}

static inline int flux_shell_run_background(
    flux_shell_state* state,
    const flux_shell_io* io,
    const char* path) {
    const ku_result_t pid = u_spawn(path);
    char line[FLUX_SHELL_OUTPUT_CAPACITY] = "opened pid=";
    char number[24];
    if (pid <= 0) {
        flux_shell_emit(io, "open: launch failed");
        state->last_status = 127;
        return 0;
    }
    if (!flux_shell_remember_job(state, (uint64_t)pid)) {
        flux_shell_emit(io, "open: job table full");
        state->last_status = 1;
        return 0;
    }
    flux_shell_format_u64(number, sizeof(number), (uint64_t)pid);
    (void)flux_shell_append(line, sizeof(line), number);
    (void)flux_shell_append(line, sizeof(line), "  ");
    (void)flux_shell_append(line, sizeof(line), path);
    flux_shell_emit(io, line);
    state->last_status = 0;
    return 1;
}

static inline int flux_shell_run_wait(
    flux_shell_state* state,
    const flux_shell_io* io,
    const char* path) {
    int32_t status = 0;
    if (!u_spawn_wait(path, &status)) {
        flux_shell_emit(io, "run: launch failed");
        state->last_status = 127;
        return 0;
    }
    state->last_status = status;
    if (status != 0) flux_shell_emit_number(io, "run: exit=", status, "");
    return status == 0;
}

static inline void flux_shell_list_jobs(
    flux_shell_state* state, const flux_shell_io* io) {
    size_t index;
    size_t active = 0U;
    flux_shell_reap_jobs(state, io, 1);
    for (index = 0U; index < FLUX_SHELL_JOB_CAPACITY; ++index) {
        if (state->jobs[index] != 0U) {
            flux_shell_emit_number(io, "job pid=", (int64_t)state->jobs[index], " running");
            ++active;
        }
    }
    if (active == 0U) flux_shell_emit(io, "jobs: none");
    state->last_status = 0;
}

static inline void flux_shell_cat(
    flux_shell_state* state,
    const flux_shell_io* io,
    const char* raw_path) {
    char path[FLUX_SHELL_PATH_CAPACITY];
    char input[256];
    char line[FLUX_SHELL_OUTPUT_CAPACITY];
    size_t line_length = 0U;
    if (!flux_shell_make_path(state, raw_path, path, sizeof(path))) {
        flux_shell_emit(io, "cat: invalid path");
        state->last_status = 2;
        return;
    }
    const ku_result_t handle = ku_open(path, flux_shell_strlen(path), KU_OPEN_READ);
    if (handle <= 0) {
        flux_shell_emit(io, "cat: cannot open file");
        state->last_status = 1;
        return;
    }
    for (;;) {
        const ku_result_t count = ku_read((ku_handle_t)handle, input, sizeof(input));
        if (count == 0) break;
        if (count < 0) {
            (void)ku_close((ku_handle_t)handle);
            flux_shell_emit(io, "cat: read failed");
            state->last_status = 1;
            return;
        }
        for (size_t index = 0U; index < (size_t)count; ++index) {
            const unsigned char character = (unsigned char)input[index];
            if (character == '\r') continue;
            if (character == '\n' || line_length + 1U >= sizeof(line)) {
                line[line_length] = '\0';
                flux_shell_emit(io, line);
                line_length = 0U;
                continue;
            }
            line[line_length++] = character >= 32U && character <= 126U
                ? (char)character : '.';
        }
    }
    (void)ku_close((ku_handle_t)handle);
    if (line_length != 0U) {
        line[line_length] = '\0';
        flux_shell_emit(io, line);
    }
    state->last_status = 0;
}

static inline void flux_shell_cd(flux_shell_state* state, const char* path) {
    if (path == (const char*)0 || path[0] == '\0' || flux_shell_streq(path, "/")) {
        (void)flux_shell_copy(state->cwd, sizeof(state->cwd), "/");
        state->last_status = 0;
        return;
    }
    if (flux_shell_streq(path, ".")) {
        state->last_status = 0;
        return;
    }
    if (flux_shell_streq(path, "..")) {
        size_t length = flux_shell_strlen(state->cwd);
        if (length <= 1U) {
            (void)flux_shell_copy(state->cwd, sizeof(state->cwd), "/");
        } else {
            while (length > 1U && state->cwd[length - 1U] != '/') --length;
            if (length <= 1U) (void)flux_shell_copy(state->cwd, sizeof(state->cwd), "/");
            else state->cwd[length - 1U] = '\0';
        }
        state->last_status = 0;
        return;
    }
    if (path[0] == '/') {
        state->last_status = flux_shell_copy(state->cwd, sizeof(state->cwd), path) ? 0 : 2;
    } else {
        char resolved[FLUX_SHELL_PATH_CAPACITY];
        state->last_status =
            flux_shell_make_path(state, path, resolved, sizeof(resolved)) &&
            flux_shell_copy(state->cwd, sizeof(state->cwd), resolved) ? 0 : 2;
    }
}

static inline void flux_shell_help(const flux_shell_io* io) {
    flux_shell_emit(io, "workspace: help clear version uname pid whoami status");
    flux_shell_emit(io, "history jobs wait pwd cd cat read which apps");
    flux_shell_emit(io, "execute: run <app|/path> open <app|/path>");
    flux_shell_emit(io, "utility: echo calc sleep yield true false exit");
}

static inline void flux_shell_apps(const flux_shell_io* io) {
    flux_shell_emit(io, "apps: shell hello external files monitor about");
}

static inline void flux_shell_which(
    flux_shell_state* state,
    const flux_shell_io* io,
    const char* name) {
    char path[FLUX_SHELL_PATH_CAPACITY];
    if (name == (const char*)0 || name[0] == '\0') {
        flux_shell_emit(io, "usage: which <app|/path>");
        state->last_status = 2;
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

static inline void flux_shell_calc(
    flux_shell_state* state,
    const flux_shell_io* io,
    char* arguments) {
    char* left_text = flux_shell_trim_left(arguments);
    char* cursor = left_text;
    char* operator_text;
    char* right_text;
    int64_t left;
    int64_t right;
    int64_t result;

    while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') ++cursor;
    if (*cursor == '\0') goto usage;
    *cursor++ = '\0';
    operator_text = flux_shell_trim_left(cursor);
    cursor = operator_text;
    while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') ++cursor;
    if (*cursor == '\0') goto usage;
    *cursor++ = '\0';
    right_text = flux_shell_trim_left(cursor);
    if (right_text[0] == '\0' || operator_text[0] == '\0' || operator_text[1] != '\0') goto usage;
    if (!flux_shell_parse_i64(left_text, &left) || !flux_shell_parse_i64(right_text, &right)) goto usage;

    switch (operator_text[0]) {
        case '+':
            if ((right > 0 && left > INT64_MAX - right) ||
                (right < 0 && left < INT64_MIN - right)) goto overflow;
            result = left + right;
            break;
        case '-':
            if ((right < 0 && left > INT64_MAX + right) ||
                (right > 0 && left < INT64_MIN + right)) goto overflow;
            result = left - right;
            break;
        case '*':
            if (left != 0 && right != 0) {
                if ((left == -1 && right == INT64_MIN) ||
                    (right == -1 && left == INT64_MIN)) goto overflow;
                if ((left > 0 && right > 0 && left > INT64_MAX / right) ||
                    (left > 0 && right < 0 && right < INT64_MIN / left) ||
                    (left < 0 && right > 0 && left < INT64_MIN / right) ||
                    (left < 0 && right < 0 && left < INT64_MAX / right)) goto overflow;
            }
            result = left * right;
            break;
        case '/':
            if (right == 0) {
                flux_shell_emit(io, "calc: division by zero");
                state->last_status = 2;
                return;
            }
            if (left == INT64_MIN && right == -1) goto overflow;
            result = left / right;
            break;
        case '%':
            if (right == 0) {
                flux_shell_emit(io, "calc: division by zero");
                state->last_status = 2;
                return;
            }
            result = (left == INT64_MIN && right == -1) ? 0 : left % right;
            break;
        default:
            goto usage;
    }
    flux_shell_emit_number(io, "", result, "");
    state->last_status = 0;
    return;

overflow:
    flux_shell_emit(io, "calc: integer overflow");
    state->last_status = 2;
    return;
usage:
    flux_shell_emit(io, "usage: calc <a> <+|-|*|/|%> <b>");
    state->last_status = 2;
}

static inline int flux_shell_requires_capability(const char* command) {
    return flux_shell_streq(command, "net") || flux_shell_streq(command, "ip") ||
        flux_shell_streq(command, "ifconfig") || flux_shell_streq(command, "route") ||
        flux_shell_streq(command, "arp") || flux_shell_streq(command, "ping") ||
        flux_shell_streq(command, "nslookup") || flux_shell_streq(command, "date") ||
        flux_shell_streq(command, "uptime") || flux_shell_streq(command, "ls") ||
        flux_shell_streq(command, "stat") || flux_shell_streq(command, "touch") ||
        flux_shell_streq(command, "mkdir") || flux_shell_streq(command, "rmdir") ||
        flux_shell_streq(command, "write") || flux_shell_streq(command, "cp") ||
        flux_shell_streq(command, "mv") || flux_shell_streq(command, "rm") ||
        flux_shell_streq(command, "reboot") || flux_shell_streq(command, "poweroff") ||
        flux_shell_streq(command, "shutdown") || flux_shell_streq(command, "mem") ||
        flux_shell_streq(command, "free") || flux_shell_streq(command, "tasks") ||
        flux_shell_streq(command, "pci") || flux_shell_streq(command, "device") ||
        flux_shell_streq(command, "driver") || flux_shell_streq(command, "diskinfo");
}

static inline flux_shell_action flux_shell_execute(
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
    flux_shell_remember_history(state, input_line);

    command = line;
    separator = command;
    while (*separator != '\0' && *separator != ' ' && *separator != '\t') ++separator;
    if (*separator != '\0') {
        *separator++ = '\0';
        arguments = flux_shell_trim_left(separator);
    } else {
        arguments = separator;
    }

    if (flux_shell_streq(command, "help")) {
        flux_shell_help(io);
        state->last_status = 0;
    } else if (flux_shell_streq(command, "clear")) {
        state->last_status = 0;
        return FLUX_SHELL_ACTION_CLEAR;
    } else if (flux_shell_streq(command, "version") || flux_shell_streq(command, "uname")) {
        flux_shell_emit(io, KUROGANE_PRODUCT_STRING " x86_64 UEFI / Ring 3");
        state->last_status = 0;
    } else if (flux_shell_streq(command, "pid")) {
        char line_out[FLUX_SHELL_OUTPUT_CAPACITY] = "pid=";
        char number[24];
        flux_shell_format_u64(number, sizeof(number), ku_getpid());
        (void)flux_shell_append(line_out, sizeof(line_out), number);
        (void)flux_shell_append(line_out, sizeof(line_out), " tid=");
        flux_shell_format_u64(number, sizeof(number), ku_gettid());
        (void)flux_shell_append(line_out, sizeof(line_out), number);
        flux_shell_emit(io, line_out);
        state->last_status = 0;
    } else if (flux_shell_streq(command, "whoami")) {
        flux_shell_emit(io, "user");
        state->last_status = 0;
    } else if (flux_shell_streq(command, "status")) {
        flux_shell_emit_number(io, "last_status=", state->last_status, "");
    } else if (flux_shell_streq(command, "history")) {
        flux_shell_show_history(state, io);
        state->last_status = 0;
    } else if (flux_shell_streq(command, "jobs")) {
        flux_shell_list_jobs(state, io);
    } else if (flux_shell_streq(command, "wait")) {
        int64_t pid = 0;
        int32_t status = 0;
        if (!flux_shell_parse_i64(arguments, &pid) || pid <= 0) {
            flux_shell_emit(io, "usage: wait <pid>");
            state->last_status = 2;
        } else if (!u_wait((uint64_t)pid, &status)) {
            flux_shell_emit(io, "wait: process is not a waitable child");
            state->last_status = 1;
        } else {
            for (size_t index = 0U; index < FLUX_SHELL_JOB_CAPACITY; ++index) {
                if (state->jobs[index] == (uint64_t)pid) state->jobs[index] = 0U;
            }
            state->last_status = status;
            flux_shell_emit_number(io, "wait: exit=", status, "");
        }
    } else if (flux_shell_streq(command, "pwd")) {
        flux_shell_emit(io, state->cwd);
        state->last_status = 0;
    } else if (flux_shell_streq(command, "cd")) {
        flux_shell_cd(state, arguments);
        if (state->last_status != 0) flux_shell_emit(io, "cd: path too long");
    } else if (flux_shell_streq(command, "cat") || flux_shell_streq(command, "read")) {
        if (arguments[0] == '\0') {
            flux_shell_emit(io, "usage: cat <path>");
            state->last_status = 2;
        } else {
            flux_shell_cat(state, io, arguments);
        }
    } else if (flux_shell_streq(command, "which")) {
        flux_shell_which(state, io, arguments);
    } else if (flux_shell_streq(command, "apps")) {
        flux_shell_apps(io);
        state->last_status = 0;
    } else if (flux_shell_streq(command, "run")) {
        char path[FLUX_SHELL_PATH_CAPACITY];
        if (!flux_shell_root_path("/apps/", arguments, path, sizeof(path))) {
            flux_shell_emit(io, "usage: run <app|/path>");
            state->last_status = 2;
        } else {
            (void)flux_shell_run_wait(state, io, path);
        }
    } else if (flux_shell_streq(command, "open")) {
        char path[FLUX_SHELL_PATH_CAPACITY];
        if (arguments[0] == '\0') {
            flux_shell_emit(io, "usage: open <app|/path>");
            state->last_status = 2;
        } else if (arguments[0] == '/') {
            (void)flux_shell_run_background(state, io, arguments);
        } else if (flux_shell_root_path("/apps/", arguments, path, sizeof(path))) {
            (void)flux_shell_run_background(state, io, path);
        }
    } else if (flux_shell_streq(command, "hello") || flux_shell_streq(command, "external") ||
               flux_shell_streq(command, "files") || flux_shell_streq(command, "monitor") ||
               flux_shell_streq(command, "about")) {
        char path[FLUX_SHELL_PATH_CAPACITY];
        if (flux_shell_root_path("/apps/", command, path, sizeof(path))) {
            (void)flux_shell_run_wait(state, io, path);
        }
    } else if (flux_shell_streq(command, "echo")) {
        flux_shell_emit(io, arguments);
        state->last_status = 0;
    } else if (flux_shell_streq(command, "calc")) {
        flux_shell_calc(state, io, arguments);
    } else if (flux_shell_streq(command, "sleep")) {
        int64_t ticks = 0;
        if (!flux_shell_parse_i64(arguments, &ticks) || ticks < 0) {
            flux_shell_emit(io, "usage: sleep <ticks>");
            state->last_status = 2;
        } else {
            state->last_status = ku_sleep((uint64_t)ticks) == KU_STATUS_OK ? 0 : 1;
        }
    } else if (flux_shell_streq(command, "yield")) {
        state->last_status = ku_yield() == KU_STATUS_OK ? 0 : 1;
    } else if (flux_shell_streq(command, "true")) {
        state->last_status = 0;
    } else if (flux_shell_streq(command, "false")) {
        state->last_status = 1;
    } else if (flux_shell_streq(command, "exit")) {
        return FLUX_SHELL_ACTION_EXIT;
    } else if (flux_shell_requires_capability(command)) {
        flux_shell_emit(io, "command requires a dedicated Ring-3 capability syscall");
        flux_shell_emit(io, "privileged kernel-console backdoors are intentionally disabled");
        state->last_status = 126;
    } else {
        char line_out[FLUX_SHELL_OUTPUT_CAPACITY] = "command not found: ";
        (void)flux_shell_append(line_out, sizeof(line_out), command);
        flux_shell_emit(io, line_out);
        state->last_status = 127;
    }
    return FLUX_SHELL_ACTION_NONE;
}

#endif

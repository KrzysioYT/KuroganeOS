#ifndef KUROGANE_SHELL_PATH_COMPLETION_H
#define KUROGANE_SHELL_PATH_COMPLETION_H

#include "shell_core.h"

enum ku_shell_completion_result {
    KU_SHELL_COMPLETION_NONE = 0,
    KU_SHELL_COMPLETION_APPLIED = 1,
    KU_SHELL_COMPLETION_AMBIGUOUS = 2
};

static inline int ku_shell_path_command(const char* command, int* apps_root,
                                        int* directories_only) {
    *apps_root = 0;
    *directories_only = 0;
    if (flux_shell_streq(command, "cd") || flux_shell_streq(command, "ls")) {
        *directories_only = 1;
        return 1;
    }
    if (flux_shell_streq(command, "cat") || flux_shell_streq(command, "read")) {
        return 1;
    }
    if (flux_shell_streq(command, "run") || flux_shell_streq(command, "open") ||
        flux_shell_streq(command, "which")) {
        *apps_root = 1;
        return 1;
    }
    return 0;
}

/* Ring-3 VFS completion: only one unquoted path argument, at end of input.
 * No guessed matches are inserted if the directory is unavailable or a
 * complete scan cannot be performed. The filesystem is never modified. */
static inline int ku_shell_complete_path(
    const flux_shell_state* state, char* input, size_t capacity, size_t* cursor) {
    size_t length = 0U;
    size_t word_start = 0U;
    size_t command_end;
    size_t argument_start;
    size_t fragment_start;
    size_t prefix_length;
    size_t common_length = 0U;
    size_t matches = 0U;
    size_t scanned = 0U;
    char command[16];
    char parent[FLUX_SHELL_PATH_CAPACITY];
    char shared[KU_FILE_NAME_CAPACITY];
    uint32_t shared_type = KU_FILE_TYPE_UNKNOWN;
    int apps_root;
    int directories_only;
    ku_result_t opened;
    int error = 0;

    if (state == (const flux_shell_state*)0 || input == (char*)0 ||
        cursor == (size_t*)0 || capacity < 3U) return KU_SHELL_COMPLETION_NONE;
    while (length < capacity && input[length] != '\0') ++length;
    if (length == capacity || *cursor != length) return KU_SHELL_COMPLETION_NONE;
    while (word_start < length &&
           (input[word_start] == ' ' || input[word_start] == '\t')) ++word_start;
    command_end = word_start;
    while (command_end < length && input[command_end] != ' ' &&
           input[command_end] != '\t') ++command_end;
    if (command_end == length || command_end == word_start ||
        command_end - word_start >= sizeof(command)) return KU_SHELL_COMPLETION_NONE;
    for (size_t index = 0U; index < command_end - word_start; ++index) {
        command[index] = input[word_start + index];
    }
    command[command_end - word_start] = '\0';
    if (!ku_shell_path_command(command, &apps_root, &directories_only)) {
        return KU_SHELL_COMPLETION_NONE;
    }

    argument_start = command_end;
    while (argument_start < length &&
           (input[argument_start] == ' ' || input[argument_start] == '\t')) {
        ++argument_start;
    }
    for (size_t index = argument_start; index < length; ++index) {
        if (input[index] == ' ' || input[index] == '\t') {
            return KU_SHELL_COMPLETION_NONE;
        }
    }
    fragment_start = argument_start;
    for (size_t index = argument_start; index < length; ++index) {
        if (input[index] == '/') fragment_start = index + 1U;
    }
    prefix_length = length - fragment_start;
    if (prefix_length >= sizeof(shared)) return KU_SHELL_COMPLETION_NONE;

    /* Everything before the last slash is the lookup directory. For app
     * commands, bare relative names intentionally resolve under /apps/. */
    parent[0] = '\0';
    if (fragment_start == argument_start) {
        if (apps_root) {
            if (!flux_shell_copy(parent, sizeof(parent), "/apps/")) {
                return KU_SHELL_COMPLETION_NONE;
            }
        } else if (!flux_shell_copy(parent, sizeof(parent), state->cwd)) {
            return KU_SHELL_COMPLETION_NONE;
        }
    } else {
        const size_t prefix_bytes = fragment_start - argument_start;
        char base[FLUX_SHELL_PATH_CAPACITY];
        if (prefix_bytes >= sizeof(base)) return KU_SHELL_COMPLETION_NONE;
        for (size_t index = 0U; index < prefix_bytes; ++index) {
            base[index] = input[argument_start + index];
        }
        base[prefix_bytes] = '\0';
        if (base[0] == '/') {
            if (!flux_shell_copy(parent, sizeof(parent), base)) {
                return KU_SHELL_COMPLETION_NONE;
            }
        } else if (apps_root) {
            if (!flux_shell_copy(parent, sizeof(parent), "/apps/") ||
                !flux_shell_append(parent, sizeof(parent), base)) {
                return KU_SHELL_COMPLETION_NONE;
            }
        } else if (!flux_shell_make_path(state, base, parent, sizeof(parent))) {
            return KU_SHELL_COMPLETION_NONE;
        }
    }

    opened = ku_file_open_ex(parent, flux_shell_strlen(parent),
        KU_FILE_OPEN_READ | KU_FILE_OPEN_DIRECTORY);
    if (opened < 0) return KU_SHELL_COMPLETION_NONE;
    for (;;) {
        ku_directory_entry entry;
        const ku_status_t result = ku_file_readdir((ku_file_t)opened, &entry);
        if (result == KU_STATUS_END_OF_STREAM) break;
        if (result != KU_STATUS_OK) {
            error = 1;
            break;
        }
        if (++scanned > 8192U) {
            error = 1;
            break;
        }
        if (entry.name_length == 0U || entry.name_length >= KU_FILE_NAME_CAPACITY) {
            continue;
        }
        entry.name[entry.name_length] = '\0';
        if (flux_shell_streq(entry.name, ".") ||
            flux_shell_streq(entry.name, "..")) continue;
        if (directories_only && entry.type != KU_FILE_TYPE_DIRECTORY &&
            entry.type != KU_FILE_TYPE_MOUNT_POINT) continue;
        size_t matched = 0U;
        while (matched < prefix_length && entry.name[matched] != '\0' &&
               entry.name[matched] == input[fragment_start + matched]) ++matched;
        if (matched != prefix_length) continue;
        if (matches == 0U) {
            if (!flux_shell_copy(shared, sizeof(shared), entry.name)) {
                error = 1;
                break;
            }
            shared_type = entry.type;
            common_length = flux_shell_strlen(shared);
        } else {
            size_t prefix = 0U;
            while (prefix < common_length && entry.name[prefix] != '\0' &&
                   shared[prefix] == entry.name[prefix]) ++prefix;
            common_length = prefix;
        }
        ++matches;
    }
    (void)ku_file_close((ku_file_t)opened);
    if (error || matches == 0U) return KU_SHELL_COMPLETION_NONE;
    const size_t additional = common_length - prefix_length;
    const int add_suffix = matches == 1U;
    if (additional + (size_t)add_suffix > capacity - length - 1U) {
        return KU_SHELL_COMPLETION_NONE;
    }
    if (matches > 1U && additional == 0U) return KU_SHELL_COMPLETION_AMBIGUOUS;
    for (size_t index = prefix_length; index < common_length; ++index) {
        input[length++] = shared[index];
    }
    if (add_suffix) {
        input[length++] = shared_type == KU_FILE_TYPE_DIRECTORY ||
            shared_type == KU_FILE_TYPE_MOUNT_POINT ? '/' : ' ';
    }
    input[length] = '\0';
    *cursor = length;
    return KU_SHELL_COMPLETION_APPLIED;
}

#endif

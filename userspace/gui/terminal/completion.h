#ifndef KUROGANE_TERMINAL_COMPLETION_H
#define KUROGANE_TERMINAL_COMPLETION_H

#include <stddef.h>

typedef enum terminal_completion_result {
    TERMINAL_COMPLETION_NONE = 0,
    TERMINAL_COMPLETION_APPLIED = 1,
    TERMINAL_COMPLETION_AMBIGUOUS = 2
} terminal_completion_result;

/* Complete the first shell word without discarding arguments or the caret.
 * Return AMBIGUOUS when Tab cannot extend a shared prefix. */
static inline terminal_completion_result terminal_complete_command(
    char* input, size_t capacity, size_t* cursor) {
    static const char* const commands[] = {
        "help", "clear", "version", "uname", "pid", "whoami", "status",
        "history", "jobs", "wait", "pwd", "cd", "ls", "cat", "read",
        "which", "apps", "run", "open", "hello", "external", "files",
        "monitor", "about", "echo", "calc", "sleep", "yield", "true",
        "false", "exit"
    };
    size_t length = 0U;
    size_t start = 0U;
    size_t end;
    size_t prefix_length;
    size_t common_length = 0U;
    size_t matches = 0U;
    const char* first = (const char*)0;
    size_t target_length;
    size_t replace_length;
    size_t additional;
    int add_space;

    if (input == (char*)0 || cursor == (size_t*)0 || capacity < 2U) {
        return TERMINAL_COMPLETION_NONE;
    }
    while (length < capacity && input[length] != '\0') ++length;
    if (length == capacity || *cursor > length) return TERMINAL_COMPLETION_NONE;
    while (start < length && (input[start] == ' ' || input[start] == '\t')) ++start;
    if (*cursor <= start) return TERMINAL_COMPLETION_NONE;
    end = start;
    while (end < length && input[end] != ' ' && input[end] != '\t') ++end;
    if (*cursor != end) return TERMINAL_COMPLETION_NONE;
    prefix_length = end - start;

    for (size_t index = 0U; index < sizeof(commands) / sizeof(commands[0]); ++index) {
        const char* word = commands[index];
        size_t position = 0U;
        while (position < prefix_length && word[position] != '\0' &&
               word[position] == input[start + position]) ++position;
        if (position != prefix_length) continue;
        if (first == (const char*)0) {
            first = word;
            while (first[common_length] != '\0') ++common_length;
        } else {
            size_t shared = 0U;
            while (shared < common_length && word[shared] != '\0' &&
                   word[shared] == first[shared]) ++shared;
            common_length = shared;
        }
        ++matches;
    }
    if (matches == 0U) return TERMINAL_COMPLETION_NONE;
    target_length = common_length;
    if (matches > 1U && target_length == prefix_length) {
        return TERMINAL_COMPLETION_AMBIGUOUS;
    }
    replace_length = end - start;
    additional = target_length - replace_length;
    add_space = matches == 1U && end == length;
    if (additional + (size_t)add_space > capacity - length - 1U) {
        return TERMINAL_COMPLETION_NONE;
    }
    /* Shift the suffix including its NUL terminator before filling the gap. */
    for (size_t offset = length - end + 1U; offset > 0U; --offset) {
        input[end + additional + (size_t)add_space + offset - 1U] =
            input[end + offset - 1U];
    }
    for (size_t index = replace_length; index < target_length; ++index) {
        input[start + index] = first[index];
    }
    if (add_space) input[start + target_length] = ' ';
    *cursor = start + target_length + (size_t)add_space;
    return TERMINAL_COMPLETION_APPLIED;
}

#endif

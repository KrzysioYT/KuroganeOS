#ifndef KUROGANE_FLUX_FILES_HISTORY_H
#define KUROGANE_FLUX_FILES_HISTORY_H

#include <stddef.h>

#define FLUX_FILES_HISTORY_CAPACITY 16U
#define FLUX_FILES_HISTORY_PATH_CAPACITY 192U
#define FLUX_FILES_HISTORY_QUERY_CAPACITY 32U

typedef struct flux_files_history_entry {
    char path[FLUX_FILES_HISTORY_PATH_CAPACITY];
    char query[FLUX_FILES_HISTORY_QUERY_CAPACITY];
} flux_files_history_entry;

typedef struct flux_files_history {
    flux_files_history_entry entries[FLUX_FILES_HISTORY_CAPACITY];
    size_t count;
    size_t current;
} flux_files_history;

static inline int flux_files_history_copy(
    char* output, size_t capacity, const char* input) {
    size_t used = 0U;
    if (output == (char*)0 || input == (const char*)0 || capacity == 0U) return 0;
    while (input[used] != '\0') {
        if (used + 1U >= capacity) return 0;
        ++used;
    }
    for (size_t index = 0U; index <= used; ++index) output[index] = input[index];
    return 1;
}

static inline int flux_files_history_init(
    flux_files_history* history, const char* path) {
    if (history == (flux_files_history*)0 || path == (const char*)0 ||
        path[0] != '/' || !flux_files_history_copy(
            history->entries[0].path, sizeof(history->entries[0].path), path)) return 0;
    history->entries[0].query[0] = '\0';
    history->count = 1U;
    history->current = 0U;
    return 1;
}

static inline int flux_files_history_filter(
    flux_files_history* history, const char* query) {
    if (history == (flux_files_history*)0 || history->count == 0U ||
        history->current >= history->count) return 0;
    return flux_files_history_copy(
        history->entries[history->current].query,
        sizeof(history->entries[history->current].query), query);
}

static inline int flux_files_history_can_move(
    const flux_files_history* history, int direction) {
    if (history == (const flux_files_history*)0 ||
        history->count == 0U || history->current >= history->count) return 0;
    return direction < 0 ? history->current > 0U :
        direction > 0 && history->current + 1U < history->count;
}

/* Peeking never commits cursor changes: callers must load/verify the VFS
 * target first. Only a successful load may call history_move(). */
static inline const flux_files_history_entry* flux_files_history_peek(
    const flux_files_history* history, int direction) {
    if (!flux_files_history_can_move(history, direction)) {
        return (const flux_files_history_entry*)0;
    }
    return &history->entries[direction < 0
        ? history->current - 1U : history->current + 1U];
}

static inline int flux_files_history_move(
    flux_files_history* history, int direction) {
    if (!flux_files_history_can_move(history, direction)) return 0;
    if (direction < 0) --history->current;
    else ++history->current;
    return 1;
}

/* Visiting a new directory discards only the forward branch; a full history
 * drops its oldest entry. All paths are absolute and bounded. */
static inline int flux_files_history_visit(
    flux_files_history* history, const char* path) {
    flux_files_history_entry next;
    size_t count;
    if (history == (flux_files_history*)0 || history->count == 0U ||
        history->current >= history->count || path == (const char*)0 ||
        path[0] != '/' || !flux_files_history_copy(next.path, sizeof(next.path), path)) {
        return 0;
    }
    if (next.path[0] == '\0') return 0;
    {
        const char* present = history->entries[history->current].path;
        size_t index = 0U;
        while (present[index] == next.path[index] && present[index] != '\0') ++index;
        if (present[index] == next.path[index]) return 1;
    }
    next.query[0] = '\0';
    count = history->current + 1U;
    if (count == FLUX_FILES_HISTORY_CAPACITY) {
        for (size_t index = 1U; index < FLUX_FILES_HISTORY_CAPACITY; ++index) {
            history->entries[index - 1U] = history->entries[index];
        }
        --count;
    }
    history->entries[count] = next;
    history->count = count + 1U;
    history->current = count;
    return 1;
}

#endif

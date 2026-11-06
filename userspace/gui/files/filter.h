#ifndef KUROGANE_FLUX_FILES_FILTER_H
#define KUROGANE_FLUX_FILES_FILTER_H

#include <stddef.h>
#include <stdint.h>
#include <kurogane/ui.h>

#define FLUX_FILES_FILTER_CAPACITY 32U

/* The existing text input ABI delivers printable ASCII scalars. Fold ASCII
 * only; preserve UTF-8 filenames byte-for-byte until Unicode text input lands. */
static inline unsigned char flux_files_fold(unsigned char value) {
    return value >= (unsigned char)'A' && value <= (unsigned char)'Z'
        ? (unsigned char)(value + ('a' - 'A')) : value;
}

static inline int flux_files_matches_filter(const char* name, const char* query) {
    if (name == (const char*)0 || query == (const char*)0) return 0;
    if (query[0] == '\0') return 1;
    for (size_t start = 0U; name[start] != '\0'; ++start) {
        size_t index = 0U;
        while (query[index] != '\0' && name[start + index] != '\0' &&
               flux_files_fold((unsigned char)name[start + index]) ==
                   flux_files_fold((unsigned char)query[index])) {
            ++index;
        }
        if (query[index] == '\0') return 1;
    }
    return 0;
}

/* Consume only search editing keys. When the query is already empty, Escape
 * and Backspace must remain available for the existing parent-folder action. */
static inline int flux_files_filter_edit(
    char* query, size_t capacity, const ku_ui_event* event) {
    size_t used = 0U;
    if (query == (char*)0 || event == (const ku_ui_event*)0 || capacity < 2U ||
        event->type != KU_UI_EVENT_KEY) return 0;
    while (used < capacity && query[used] != '\0') ++used;
    if (used == capacity) return 0;
    if ((event->modifiers & (KU_UI_MOD_CONTROL | KU_UI_MOD_ALT)) != 0U) {
        return 0;
    }
    if (event->key == KU_UI_KEY_ESCAPE && used != 0U) {
        query[0] = '\0';
        return 1;
    }
    if (event->key == KU_UI_KEY_BACKSPACE && used != 0U) {
        query[used - 1U] = '\0';
        return 1;
    }
    if (event->key == KU_UI_KEY_UNKNOWN || event->character < 32U ||
        event->character > 126U || used + 1U >= capacity) return 0;
    query[used] = (char)event->character;
    query[used + 1U] = '\0';
    return 1;
}

#endif

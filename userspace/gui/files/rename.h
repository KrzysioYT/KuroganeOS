#ifndef KUROGANE_FLUX_FILES_RENAME_H
#define KUROGANE_FLUX_FILES_RENAME_H

#include <stddef.h>
#include <kurogane/filesystem.h>
#include "folder_creation.h"

typedef enum flux_files_rename_policy {
    FLUX_FILES_RENAME_INVALID = 0,
    FLUX_FILES_RENAME_UNCHANGED,
    FLUX_FILES_RENAME_READY
} flux_files_rename_policy;

/* Rename only normal files and directories, never devices or mount points.
 * Reuse the bounded, path-segment-only editor; a rename cannot move an item. */
static inline flux_files_rename_policy flux_files_rename_check(
    uint32_t type, const char* source, const char* destination,
    size_t capacity) {
    size_t index = 0U;
    if ((type != KU_FILE_TYPE_REGULAR && type != KU_FILE_TYPE_DIRECTORY) ||
        source == (const char*)0 || destination == (const char*)0 ||
        !flux_files_folder_valid_name(destination, capacity)) {
        return FLUX_FILES_RENAME_INVALID;
    }
    /* Source is a VFS directory entry and can have non-ASCII bytes, but may
     * never be an unterminated name or a path segment such as '.' or '..'. */
    while (index < capacity && source[index] != '\0') {
        if (source[index] == '/' || source[index] == '\\') return FLUX_FILES_RENAME_INVALID;
        ++index;
    }
    if (index == 0U || index == capacity ||
        (index == 1U && source[0] == '.') ||
        (index == 2U && source[0] == '.' && source[1] == '.')) {
        return FLUX_FILES_RENAME_INVALID;
    }
    index = 0U;
    while (index < capacity && source[index] != '\0' &&
           destination[index] != '\0' &&
           source[index] == destination[index]) ++index;
    if (index < capacity && source[index] == '\0' &&
        destination[index] == '\0') return FLUX_FILES_RENAME_UNCHANGED;
    return FLUX_FILES_RENAME_READY;
}

#endif

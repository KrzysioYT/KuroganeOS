#ifndef KUROGANE_FLUX_FILES_FOLDER_CREATION_H
#define KUROGANE_FLUX_FILES_FOLDER_CREATION_H

#include <stddef.h>
#include <stdint.h>
#include <kurogane/ui.h>

#define FLUX_FILES_NEW_FOLDER_CAPACITY 64U

typedef enum flux_files_folder_action {
    FLUX_FILES_FOLDER_NONE = 0,
    FLUX_FILES_FOLDER_EDITED,
    FLUX_FILES_FOLDER_SUBMIT,
    FLUX_FILES_FOLDER_CANCEL,
    FLUX_FILES_FOLDER_INVALID,
    FLUX_FILES_FOLDER_FULL
} flux_files_folder_action;

/* Portable, ASCII-only directory names until the native text widget gains
 * UTF-8 editing. Never let a name escape the selected VFS directory. */
static inline int flux_files_folder_character(uint32_t value) {
    return (value >= 'a' && value <= 'z') ||
        (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9') ||
        value == '_' || value == '-' || value == '.' || value == ' ';
}

static inline int flux_files_folder_valid_name(const char* name, size_t capacity) {
    size_t used = 0U;
    if (name == (const char*)0 || capacity < 2U) return 0;
    while (used < capacity && name[used] != '\0') {
        if (!flux_files_folder_character((unsigned char)name[used])) return 0;
        ++used;
    }
    if (used == 0U || used == capacity || name[0] == ' ' ||
        name[used - 1U] == ' ' ||
        (used == 1U && name[0] == '.') ||
        (used == 2U && name[0] == '.' && name[1] == '.')) return 0;
    return 1;
}

static inline flux_files_folder_action flux_files_folder_key(
    char* name, size_t capacity, const ku_ui_event* event) {
    size_t used = 0U;
    if (name == (char*)0 || event == (const ku_ui_event*)0 ||
        event->type != KU_UI_EVENT_KEY || capacity < 2U ||
        (event->modifiers & (KU_UI_MOD_CONTROL | KU_UI_MOD_ALT)) != 0U) {
        return FLUX_FILES_FOLDER_NONE;
    }
    while (used < capacity && name[used] != '\0') ++used;
    if (used == capacity) return FLUX_FILES_FOLDER_INVALID;
    if (event->key == KU_UI_KEY_ESCAPE) return FLUX_FILES_FOLDER_CANCEL;
    if (event->key == KU_UI_KEY_ENTER) return FLUX_FILES_FOLDER_SUBMIT;
    if (event->key == KU_UI_KEY_BACKSPACE) {
        if (used == 0U) return FLUX_FILES_FOLDER_NONE;
        name[used - 1U] = '\0';
        return FLUX_FILES_FOLDER_EDITED;
    }
    if (event->key == KU_UI_KEY_UNKNOWN || event->character == 0U) {
        return FLUX_FILES_FOLDER_NONE;
    }
    if (!flux_files_folder_character(event->character)) {
        return FLUX_FILES_FOLDER_INVALID;
    }
    if (used + 1U >= capacity) return FLUX_FILES_FOLDER_FULL;
    name[used] = (char)event->character;
    name[used + 1U] = '\0';
    return FLUX_FILES_FOLDER_EDITED;
}

#endif

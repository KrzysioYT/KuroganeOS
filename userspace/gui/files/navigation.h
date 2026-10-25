#pragma once
#include <stddef.h>
#include <stdint.h>
#include <kurogane/ui.h>

#define FLUX_FILES_NO_SELECTION UINT32_MAX
typedef enum flux_files_key_action {
    FLUX_FILES_KEY_NONE = 0, FLUX_FILES_KEY_SELECT,
    FLUX_FILES_KEY_OPEN, FLUX_FILES_KEY_PARENT, FLUX_FILES_KEY_COPY
} flux_files_key_action;

/* Pure Ring-3 keyboard navigation policy. All page offsets remain aligned.
 * Ctrl/Alt navigation is reserved for shortcuts/window manager.
 */
static inline flux_files_key_action flux_files_keyboard(
    const ku_ui_event* event, size_t count, size_t page_size,
    size_t* page_start, uint32_t* selected) {
    size_t index;
    if (event == NULL || event->type != KU_UI_EVENT_KEY ||
        page_start == NULL || selected == NULL || page_size == 0U) {
        return FLUX_FILES_KEY_NONE;
    }
    if ((event->modifiers & KU_UI_MOD_ALT) != 0U) return FLUX_FILES_KEY_NONE;
    if ((event->modifiers & KU_UI_MOD_CONTROL) != 0U) {
        return ku_ui_clipboard_shortcut(event) == 'c'
            ? FLUX_FILES_KEY_COPY : FLUX_FILES_KEY_NONE;
    }
    if (event->key == KU_UI_KEY_BACKSPACE || event->key == KU_UI_KEY_ESCAPE) {
        return FLUX_FILES_KEY_PARENT;
    }
    if (event->key == KU_UI_KEY_ENTER) return FLUX_FILES_KEY_OPEN;
    if (count == 0U || count > UINT32_MAX) return FLUX_FILES_KEY_NONE;
    index = *selected == FLUX_FILES_NO_SELECTION || (size_t)*selected >= count
        ? count : (size_t)*selected;
    switch (event->key) {
        case KU_UI_KEY_ARROW_DOWN:
        case KU_UI_KEY_ARROW_RIGHT:
            index = index == count ? 0U
                : (index + 1U < count ? index + 1U : index);
            break;
        case KU_UI_KEY_ARROW_UP:
        case KU_UI_KEY_ARROW_LEFT:
            index = index == count ? count - 1U
                : (index == 0U ? 0U : index - 1U);
            break;
        case KU_UI_KEY_HOME: index = 0U; break;
        case KU_UI_KEY_END: index = count - 1U; break;
        case KU_UI_KEY_PAGE_DOWN: {
            const size_t page = *page_start / page_size;
            const size_t last = (count - 1U) / page_size;
            index = (page < last ? page + 1U : last) * page_size;
            break;
        }
        case KU_UI_KEY_PAGE_UP: {
            const size_t page = *page_start / page_size;
            index = (page == 0U ? 0U : page - 1U) * page_size;
            break;
        }
        default: return FLUX_FILES_KEY_NONE;
    }
    *selected = (uint32_t)index;
    *page_start = (index / page_size) * page_size;
    return FLUX_FILES_KEY_SELECT;
}

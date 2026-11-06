#ifndef KUROGANE_FLUX_SETTINGS_NAVIGATION_H
#define KUROGANE_FLUX_SETTINGS_NAVIGATION_H

#include <kurogane/libui.h>

typedef enum flux_settings_key_action {
    FLUX_SETTINGS_KEY_NONE = 0,
    FLUX_SETTINGS_KEY_NEXT,
    FLUX_SETTINGS_KEY_PREVIOUS,
    FLUX_SETTINGS_KEY_ACTIVATE,
    FLUX_SETTINGS_KEY_PAGE_UP,
    FLUX_SETTINGS_KEY_PAGE_DOWN
} flux_settings_key_action;

static inline flux_settings_key_action flux_settings_keyboard(const ku_ui_event* event) {
    if (event == (const ku_ui_event*)0 || event->type != KU_UI_EVENT_KEY ||
        (event->modifiers & (KU_UI_MOD_CONTROL | KU_UI_MOD_ALT)) != 0U) {
        return FLUX_SETTINGS_KEY_NONE;
    }
    if (event->key == KU_UI_KEY_TAB) {
        return (event->modifiers & KU_UI_MOD_SHIFT) != 0U
            ? FLUX_SETTINGS_KEY_PREVIOUS : FLUX_SETTINGS_KEY_NEXT;
    }
    if (event->key == KU_UI_KEY_ARROW_DOWN || event->key == KU_UI_KEY_ARROW_RIGHT) {
        return FLUX_SETTINGS_KEY_NEXT;
    }
    if (event->key == KU_UI_KEY_ARROW_UP || event->key == KU_UI_KEY_ARROW_LEFT) {
        return FLUX_SETTINGS_KEY_PREVIOUS;
    }
    if (event->key == KU_UI_KEY_PAGE_UP) return FLUX_SETTINGS_KEY_PAGE_UP;
    if (event->key == KU_UI_KEY_PAGE_DOWN) return FLUX_SETTINGS_KEY_PAGE_DOWN;
    if (event->key == KU_UI_KEY_ENTER ||
        (event->key != KU_UI_KEY_UNKNOWN && event->character == ' ')) {
        return FLUX_SETTINGS_KEY_ACTIVATE;
    }
    return FLUX_SETTINGS_KEY_NONE;
}

/* Keep the selected interactive control in the retained-scene viewport.
 * Hidden rows do not count; disabled rows can appear but are never focused. */
static inline void flux_settings_reveal_selection(kui_scene* scene) {
    uint32_t visible = 0U;
    uint32_t selected_position = UINT32_MAX;
    uint32_t rows;
    if (scene == (kui_scene*)0 || scene->selected_id == 0U) return;
    rows = scene->visible_rows == 0U ? KU_UI_NATIVE_MAX_COMMANDS : scene->visible_rows;
    if (rows > KU_UI_NATIVE_MAX_COMMANDS) rows = KU_UI_NATIVE_MAX_COMMANDS;
    for (uint32_t index = 0U; index < scene->view_count; ++index) {
        const kui_view* view = &scene->views[index];
        if ((view->flags & KUI_VIEW_HIDDEN) != 0U) continue;
        if (view->id == scene->selected_id) selected_position = visible;
        ++visible;
    }
    if (selected_position == UINT32_MAX || visible <= rows) {
        scene->scroll_offset = 0U;
        return;
    }
    if (selected_position < scene->scroll_offset) {
        scene->scroll_offset = selected_position;
    } else if (selected_position >= scene->scroll_offset + rows) {
        scene->scroll_offset = selected_position - rows + 1U;
    }
    if (scene->scroll_offset > visible - rows) scene->scroll_offset = visible - rows;
}

#endif

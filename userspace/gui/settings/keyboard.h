#ifndef KUROGANE_SETTINGS_KEYBOARD_H
#define KUROGANE_SETTINGS_KEYBOARD_H
#include <kurogane/libui.h>

/* Return 1 for focus navigation, 2 for activation, 0 for unrelated input. */
static inline int flux_settings_key(kui_scene* scene, const ku_ui_event* event,
                                    uint32_t* target) {
    uint32_t index, row = 0U;
    uint32_t rows;
    if (!scene || !event || !target || event->type != KU_UI_EVENT_KEY ||
        (event->modifiers & (KU_UI_MOD_CONTROL | KU_UI_MOD_ALT))) return 0;
    *target = 0U;
    if (event->key != KU_UI_KEY_TAB) {
        if (event->key != KU_UI_KEY_ENTER && event->character != ' ') return 0;
        *target = kui_scene_selected(scene);
        return *target != 0U ? 2 : 0;
    }
    if (kui_scene_select_next(scene,
            (event->modifiers & KU_UI_MOD_SHIFT) ? -1 : 1) != KU_STATUS_OK) return 0;
    rows = scene->visible_rows ? scene->visible_rows : KU_UI_MAX_LINES;
    if (rows > KU_UI_MAX_LINES) rows = KU_UI_MAX_LINES;
    for (index = 0U; index < scene->view_count; ++index) {
        if (scene->views[index].flags & KUI_VIEW_HIDDEN) continue;
        if (scene->views[index].id == kui_scene_selected(scene)) {
            if (row < scene->scroll_offset) scene->scroll_offset = row;
            else if (row - scene->scroll_offset >= rows)
                scene->scroll_offset = row - rows + 1U;
            (void)kui_scene_scroll(scene, 0);
            break;
        }
        ++row;
    }
    return 1;
}
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../userspace/gui/settings/navigation.h"

static ku_ui_event key(uint32_t code, uint32_t character, uint32_t modifiers) {
    ku_ui_event event;
    memset(&event, 0, sizeof(event));
    event.type = KU_UI_EVENT_KEY;
    event.key = code;
    event.character = character;
    event.modifiers = modifiers;
    return event;
}

static void test_keyboard(void) {
    ku_ui_event event = key(KU_UI_KEY_TAB, 0U, 0U);
    assert(flux_settings_keyboard(&event) == FLUX_SETTINGS_KEY_NEXT);
    event.modifiers = KU_UI_MOD_SHIFT;
    assert(flux_settings_keyboard(&event) == FLUX_SETTINGS_KEY_PREVIOUS);
    event = key(KU_UI_KEY_ARROW_DOWN, 0U, 0U);
    assert(flux_settings_keyboard(&event) == FLUX_SETTINGS_KEY_NEXT);
    event = key(KU_UI_KEY_ARROW_LEFT, 0U, 0U);
    assert(flux_settings_keyboard(&event) == FLUX_SETTINGS_KEY_PREVIOUS);
    event = key(KU_UI_KEY_PAGE_DOWN, 0U, 0U);
    assert(flux_settings_keyboard(&event) == FLUX_SETTINGS_KEY_PAGE_DOWN);
    event = key(KU_UI_KEY_PAGE_UP, 0U, 0U);
    assert(flux_settings_keyboard(&event) == FLUX_SETTINGS_KEY_PAGE_UP);
    event = key(KU_UI_KEY_ENTER, 0U, 0U);
    assert(flux_settings_keyboard(&event) == FLUX_SETTINGS_KEY_ACTIVATE);
    event = key(KU_UI_KEY_A, ' ', 0U);
    assert(flux_settings_keyboard(&event) == FLUX_SETTINGS_KEY_ACTIVATE);
    event = key(KU_UI_KEY_UNKNOWN, ' ', 0U);
    assert(flux_settings_keyboard(&event) == FLUX_SETTINGS_KEY_NONE);
    event.modifiers = KU_UI_MOD_CONTROL;
    assert(flux_settings_keyboard(&event) == FLUX_SETTINGS_KEY_NONE);
    event = key(KU_UI_KEY_TAB, 0U, KU_UI_MOD_ALT);
    assert(flux_settings_keyboard(&event) == FLUX_SETTINGS_KEY_NONE);
    event.type = KU_UI_EVENT_POINTER;
    assert(flux_settings_keyboard(&event) == FLUX_SETTINGS_KEY_NONE);
    assert(flux_settings_keyboard(NULL) == FLUX_SETTINGS_KEY_NONE);
}

static void test_focus_visibility(void) {
    kui_scene scene;
    memset(&scene, 0, sizeof(scene));
    scene.view_count = 15U;
    scene.visible_rows = 5U;
    for (uint32_t index = 0U; index < scene.view_count; ++index) {
        scene.views[index].id = index + 1U;
    }
    scene.views[3].flags = KUI_VIEW_HIDDEN;
    scene.selected_id = 15U;
    flux_settings_reveal_selection(&scene);
    assert(scene.scroll_offset == 9U);

    scene.selected_id = 1U;
    flux_settings_reveal_selection(&scene);
    assert(scene.scroll_offset == 0U);

    scene.selected_id = 7U;
    flux_settings_reveal_selection(&scene);
    assert(scene.scroll_offset == 1U);
    flux_settings_reveal_selection(&scene);
    assert(scene.scroll_offset == 1U);

    scene.visible_rows = 20U;
    scene.scroll_offset = 7U;
    flux_settings_reveal_selection(&scene);
    assert(scene.scroll_offset == 0U);

    scene.scroll_offset = 4U;
    scene.selected_id = 0U;
    flux_settings_reveal_selection(&scene);
    assert(scene.scroll_offset == 4U);
}

int main(void) {
    test_keyboard();
    test_focus_visibility();
    puts("Flux Settings keyboard focus and viewport: PASS");
    return 0;
}

#include <assert.h>
#include <string.h>
#include <kurogane/ui.h>
#include "../userspace/gui/files/navigation.h"
static ku_ui_event key(uint32_t code, uint32_t mods) {
    ku_ui_event e;
    memset(&e, 0, sizeof(e));
    e.structure_size = sizeof(e);
    e.type = KU_UI_EVENT_KEY; e.key = code; e.modifiers = mods;
    return e;
}
int main(void) {
    size_t page = 0U;
    uint32_t selected = FLUX_FILES_NO_SELECTION;
    ku_ui_event e = key(KU_UI_KEY_ARROW_DOWN, 0U);
    for (uint32_t i = 0U; i < 7U; ++i) {
        assert(flux_files_keyboard(&e, 14U, 6U, &page, &selected) == FLUX_FILES_KEY_SELECT);
        assert(selected == i);
    }
    assert(page == 6U);
    e = key(KU_UI_KEY_PAGE_DOWN, 0U);
    assert(flux_files_keyboard(&e, 14U, 6U, &page, &selected) == FLUX_FILES_KEY_SELECT);
    assert(page == 12U && selected == 12U);
    (void)flux_files_keyboard(&e, 14U, 6U, &page, &selected);
    assert(page == 12U && selected == 12U);
    e = key(KU_UI_KEY_END, 0U);
    (void)flux_files_keyboard(&e, 14U, 6U, &page, &selected);
    assert(page == 12U && selected == 13U);
    e = key(KU_UI_KEY_ARROW_DOWN, 0U);
    (void)flux_files_keyboard(&e, 14U, 6U, &page, &selected);
    assert(selected == 13U);
    e = key(KU_UI_KEY_PAGE_UP, 0U);
    (void)flux_files_keyboard(&e, 14U, 6U, &page, &selected);
    assert(page == 6U && selected == 6U);
    e = key(KU_UI_KEY_HOME, 0U);
    (void)flux_files_keyboard(&e, 14U, 6U, &page, &selected);
    assert(page == 0U && selected == 0U);
    e = key(KU_UI_KEY_ARROW_UP, 0U);
    (void)flux_files_keyboard(&e, 14U, 6U, &page, &selected);
    assert(selected == 0U);
    e = key(KU_UI_KEY_C, KU_UI_MOD_CONTROL);
    e.character = 3U;
    assert(flux_files_keyboard(&e, 14U, 6U, &page, &selected) == FLUX_FILES_KEY_COPY);
    assert(selected == 0U);
    e = key(KU_UI_KEY_C, KU_UI_MOD_CONTROL | KU_UI_MOD_ALT);
    assert(flux_files_keyboard(&e, 14U, 6U, &page, &selected) == FLUX_FILES_KEY_NONE);
    e = key(KU_UI_KEY_ARROW_DOWN, KU_UI_MOD_CONTROL);
    assert(flux_files_keyboard(&e, 14U, 6U, &page, &selected) == FLUX_FILES_KEY_NONE);
    e = key(KU_UI_KEY_ENTER, 0U);
    assert(flux_files_keyboard(&e, 14U, 6U, &page, &selected) == FLUX_FILES_KEY_OPEN);
    e = key(KU_UI_KEY_BACKSPACE, 0U);
    assert(flux_files_keyboard(&e, 14U, 6U, &page, &selected) == FLUX_FILES_KEY_PARENT);
    e = key(KU_UI_KEY_ESCAPE, 0U);
    assert(flux_files_keyboard(&e, 14U, 6U, &page, &selected) == FLUX_FILES_KEY_PARENT);
    e = key(KU_UI_KEY_ARROW_DOWN, 0U);
    assert(flux_files_keyboard(&e, 0U, 6U, &page, &selected) == FLUX_FILES_KEY_NONE);
    assert(flux_files_keyboard(&e, 4U, 0U, &page, &selected) == FLUX_FILES_KEY_NONE);
    e.type = KU_UI_EVENT_SCROLL; e.wheel_y = -1;
    assert(flux_files_keyboard(&e, 14U, 6U, &page, &selected) == FLUX_FILES_KEY_NONE);
    assert(flux_files_keyboard(&e, 14U, 6U, NULL, &selected) == FLUX_FILES_KEY_NONE);
    return 0;
}

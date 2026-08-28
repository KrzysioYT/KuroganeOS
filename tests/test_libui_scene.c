#include <assert.h>
#include <stddef.h>
#include <string.h>

#include <kurogane/libui.h>

/* Host libc does not guarantee BSD strlcpy; libui deliberately does. */
size_t strlcpy(char* destination, const char* source, size_t capacity) {
    const size_t length = strlen(source);
    if (capacity != 0U) {
        const size_t copied = length < capacity - 1U ? length : capacity - 1U;
        memcpy(destination, source, copied);
        destination[copied] = '\0';
    }
    return length;
}

int main(void) {
    kui_scene scene;
    kui_scene_initialize(&scene);
    scene.visible_rows = 3U;

    assert(kui_scene_add(&scene, 1U, 0U, KUI_VIEW_PANEL, "ROOT") == KU_STATUS_OK);
    assert(kui_scene_add(&scene, 2U, 1U, KUI_VIEW_BUTTON, "ONE") == KU_STATUS_OK);
    assert(kui_scene_add(&scene, 3U, 1U, KUI_VIEW_BUTTON, "TWO") == KU_STATUS_OK);
    assert(kui_scene_add(&scene, 4U, 1U, KUI_VIEW_LABEL, "DETAIL") == KU_STATUS_OK);
    assert(kui_scene_add(&scene, 5U, 1U, KUI_VIEW_BUTTON, "THREE") == KU_STATUS_OK);

    assert(kui_scene_select(&scene, 5U) == KU_STATUS_OK);
    assert(scene.scroll_offset == 2U);
    assert(kui_scene_select(&scene, 2U) == KU_STATUS_OK);
    assert(scene.scroll_offset == 1U);

    assert(kui_scene_set_flags(&scene, 3U, KUI_VIEW_HIDDEN) == KU_STATUS_OK);
    assert(kui_scene_select(&scene, 5U) == KU_STATUS_OK);
    assert(scene.scroll_offset == 1U);
    assert(kui_scene_selected(&scene) == 5U);

    assert(kui_scene_select_next(&scene, 1) == KU_STATUS_OK);
    assert(kui_scene_selected(&scene) == 2U);
    assert(scene.scroll_offset == 1U);
    return 0;
}

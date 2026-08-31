#include <kurogane/libui.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Debian/glibc used by the host runner may not provide strlcpy. */
size_t strlcpy(char* destination, const char* source, size_t capacity) {
    const size_t length = strlen(source);
    if (capacity != 0U) {
        const size_t copied = length < capacity - 1U ? length : capacity - 1U;
        memcpy(destination, source, copied);
        destination[copied] = '\0';
    }
    return length;
}

static int expect(uint32_t actual, uint32_t expected, const char* label) {
    if (actual == expected) return 1;
    printf("%s: expected %u got %u\n", label, expected, actual);
    return 0;
}

static int row_y(uint32_t row) {
    return 12 + (int)(row * 22U) + 10;
}

int main(void) {
    kui_scene scene;
    kui_flow root;
    kui_scene_initialize(&scene);
    scene.visible_rows = 4U;
    kui_flow_begin(&root, &scene, 0U);
    if (kui_flow_panel(&root, 1U, "PANEL") != KU_STATUS_OK ||
        kui_flow_button(&root, 2U, "OPEN") != KU_STATUS_OK ||
        kui_flow_label(&root, 3U, "INFO") != KU_STATUS_OK ||
        kui_flow_list_item(&root, 4U, "ITEM") != KU_STATUS_OK ||
        kui_flow_button(&root, 5U, "MORE") != KU_STATUS_OK ||
        kui_flow_button(&root, 6U, "OVERFLOW") != KU_STATUS_OK) return 1;

    if (!expect(kui_scene_hit_test(&scene, 20, row_y(0U)), 0U, "panel inert") ||
        !expect(kui_scene_hit_test(&scene, 20, row_y(1U)), 2U, "button hit") ||
        !expect(kui_scene_hit_test(&scene, 20, row_y(2U)), 0U, "label inert") ||
        !expect(kui_scene_hit_test(&scene, 20, row_y(3U)), 4U, "list hit") ||
        !expect(kui_scene_hit_test(&scene, -1, row_y(1U)), 0U, "negative x") ||
        !expect(kui_scene_hit_test(&scene, 20, 11), 0U, "top inset")) return 2;

    if (kui_scene_set_flags(&scene, 2U, KUI_VIEW_DISABLED) != KU_STATUS_OK ||
        !expect(kui_scene_hit_test(&scene, 20, row_y(1U)), 0U, "disabled inert")) return 3;
    if (kui_scene_set_flags(&scene, 2U, 0U) != KU_STATUS_OK ||
        kui_scene_set_flags(&scene, 3U, KUI_VIEW_HIDDEN) != KU_STATUS_OK ||
        !expect(kui_scene_hit_test(&scene, 20, row_y(2U)), 4U, "hidden row compaction") ||
        !expect(kui_scene_hit_test(&scene, 20, row_y(3U)), 5U, "visible fourth row")) return 4;

    if (kui_scene_scroll(&scene, 1) != KU_STATUS_OK ||
        !expect(kui_scene_hit_test(&scene, 20, row_y(0U)), 2U, "scroll row zero") ||
        !expect(kui_scene_hit_test(&scene, 20, row_y(1U)), 4U, "scroll list")) return 5;

    puts("libui mouse hit-test tests passed");
    return 0;
}

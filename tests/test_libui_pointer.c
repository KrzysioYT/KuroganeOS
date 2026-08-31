#include <kurogane/libui.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

static int center_y(const ku_ui_native_frame* frame, uint32_t command) {
    return frame->commands[command].y + frame->commands[command].height / 2;
}

int main(void) {
    kui_scene scene;
    kui_flow root;
    ku_ui_native_frame native;
    kui_scene_initialize(&scene);
    scene.visible_rows = 4U;
    kui_flow_begin(&root, &scene, 0U);
    if (kui_flow_panel(&root, 1U, "PANEL") != KU_STATUS_OK ||
        kui_flow_button(&root, 2U, "OPEN") != KU_STATUS_OK ||
        kui_flow_label(&root, 3U, "INFO") != KU_STATUS_OK ||
        kui_flow_list_item(&root, 4U, "ITEM") != KU_STATUS_OK ||
        kui_flow_button(&root, 5U, "MORE") != KU_STATUS_OK ||
        kui_flow_button(&root, 6U, "OVERFLOW") != KU_STATUS_OK) return 1;

    if (kui_scene_build_native(&scene, &native) != KU_STATUS_OK ||
        native.magic != KU_UI_NATIVE_MAGIC ||
        native.version != KU_UI_NATIVE_VERSION || native.command_count != 4U ||
        native.commands[0].type != KU_UI_NATIVE_PANEL ||
        native.commands[1].type != KU_UI_NATIVE_BUTTON ||
        native.commands[3].type != KU_UI_NATIVE_LIST_ITEM ||
        native.commands[1].width != 0 || native.commands[1].height != 34) return 2;

    if (!expect(kui_scene_hit_test(&scene, 24, center_y(&native, 0U)), 0U, "panel inert") ||
        !expect(kui_scene_hit_test(&scene, 24, center_y(&native, 1U)), 2U, "button hit") ||
        !expect(kui_scene_hit_test(&scene, 24, center_y(&native, 2U)), 0U, "label inert") ||
        !expect(kui_scene_hit_test(&scene, 24, center_y(&native, 3U)), 4U, "list hit") ||
        !expect(kui_scene_hit_test(&scene, -1, center_y(&native, 1U)), 0U, "negative x")) return 3;

    if (kui_scene_set_flags(&scene, 2U, KUI_VIEW_DISABLED) != KU_STATUS_OK ||
        !expect(kui_scene_hit_test(&scene, 24, center_y(&native, 1U)), 0U, "disabled inert")) return 4;

    if (kui_scene_set_flags(&scene, 2U, 0U) != KU_STATUS_OK ||
        kui_scene_set_flags(&scene, 3U, KUI_VIEW_HIDDEN) != KU_STATUS_OK ||
        kui_scene_build_native(&scene, &native) != KU_STATUS_OK ||
        native.command_count != 4U ||
        !expect(kui_scene_hit_test(&scene, 24, center_y(&native, 2U)), 4U, "hidden compaction") ||
        !expect(kui_scene_hit_test(&scene, 24, center_y(&native, 3U)), 5U, "visible fourth")) return 5;

    if (kui_scene_scroll(&scene, 1) != KU_STATUS_OK ||
        kui_scene_build_native(&scene, &native) != KU_STATUS_OK ||
        native.command_count != 4U ||
        native.commands[0].type != KU_UI_NATIVE_BUTTON ||
        !expect(kui_scene_hit_test(&scene, 24, center_y(&native, 0U)), 2U, "scroll first") ||
        !expect(kui_scene_hit_test(&scene, 24, center_y(&native, 1U)), 4U, "scroll list")) return 6;

    puts("libui native packet + mouse hit-test tests passed");
    return 0;
}

#include "../common.h"

static void build_scene(kui_scene* scene, int light, uint32_t selected) {
    kui_flow root;
    kui_flow appearance;
    kui_scene_initialize(scene);
    if (light) {
        kui_scene_set_palette(
            scene, UINT32_C(0xE2E8F0), UINT32_C(0x111827), UINT32_C(0xC2410C));
    }

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "SETTINGS // APPEARANCE");
    (void)kui_flow_label(&root, 2U, "Flux theme is session-local in 2.5.");
    (void)kui_flow_separator(&root, 3U);

    kui_flow_begin(&appearance, scene, 1U);
    (void)kui_flow_button(&appearance, 10U, "DARK SIGNAL");
    (void)kui_flow_button(&appearance, 11U, "LIGHT SIGNAL");
    (void)kui_flow_label(
        &appearance, 12U, light ? "ACTIVE: LIGHT SIGNAL" : "ACTIVE: DARK SIGNAL");
    (void)kui_flow_separator(&appearance, 13U);
    (void)kui_flow_label(&appearance, 14U, "J/K: focus  ENTER: apply  T: quick toggle");
    (void)kui_flow_label(&appearance, 15U, "Persistent settings service is scheduled for 3.1.");

    if (selected != 10U && selected != 11U) selected = light ? 11U : 10U;
    (void)kui_scene_select(scene, selected);
}

static int apply_selected(uint32_t selected, int current) {
    if (selected == 10U) return 0;
    if (selected == 11U) return 1;
    return current;
}

int main(void) {
    const ku_window_t window = gui_open("SETTINGS", 545, 385, 420, 280);
    if (window == KU_INVALID_WINDOW) return 1;

    int light = 0;
    uint32_t selected = 10U;
    kui_scene scene;
    build_scene(&scene, light, selected);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 2;
    }
    puts("[TEST] desktop_settings_real: PASS");
    puts("[TEST] flux_scene_settings: PASS");

    for (;;) {
        ku_ui_event event;
        if (gui_wait_event(window, &event) < 0 || event.type == KU_UI_EVENT_CLOSE) break;
        if (event.type != KU_UI_EVENT_KEY) continue;

        if (event.character == 'j' || event.character == 'J') {
            (void)kui_scene_select_next(&scene, 1);
            selected = kui_scene_selected(&scene);
        } else if (event.character == 'k' || event.character == 'K') {
            (void)kui_scene_select_next(&scene, -1);
            selected = kui_scene_selected(&scene);
        } else if (event.character == 't' || event.character == 'T') {
            light = !light;
        } else if (event.character == '\r' || event.character == '\n') {
            light = apply_selected(selected, light);
        } else {
            continue;
        }

        build_scene(&scene, light, selected);
        (void)kui_scene_present(window, &scene);
    }
    (void)ku_ui_close(window);
    return 0;
}

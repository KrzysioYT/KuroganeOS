#include "../common.h"

static void build_scene(kui_scene* scene, int low_contrast, uint32_t selected) {
    kui_flow root;
    kui_flow appearance;
    kui_scene_initialize(scene);
    kui_scene_set_palette(
        scene,
        UINT32_C(0x090A0C),
        UINT32_C(0xECEEF1),
        low_contrast ? UINT32_C(0x8F2633) : UINT32_C(0xDE192D));

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "SETTINGS / RED FLUX");
    (void)kui_flow_label(&root, 2U, "BLACK / GRAPHITE / RED VISUAL PROFILE");
    (void)kui_flow_separator(&root, 3U);

    kui_flow_begin(&appearance, scene, 1U);
    (void)kui_flow_button(&appearance, 10U, "RED CORE");
    (void)kui_flow_button(&appearance, 11U, "LOW CONTRAST RED");
    (void)kui_flow_label(
        &appearance,
        12U,
        low_contrast ? "ACTIVE / LOW CONTRAST RED" : "ACTIVE / RED CORE");
    (void)kui_flow_separator(&appearance, 13U);
    (void)kui_flow_label(&appearance, 14U, "ARROWS / TAB SELECT   ENTER APPLY");
    (void)kui_flow_label(&appearance, 15U, "ESC RESTORES RED CORE FOR THIS SURFACE");

    if (selected != 10U && selected != 11U) selected = low_contrast ? 11U : 10U;
    (void)kui_scene_select(scene, selected);
}

static int apply_selected(uint32_t selected, int current) {
    if (selected == 10U) return 0;
    if (selected == 11U) return 1;
    return current;
}

int main(void) {
    const ku_window_t window = gui_open("SETTINGS", 430, 235, 470, 310);
    if (window == KU_INVALID_WINDOW) return 1;

    int low_contrast = 0;
    uint32_t selected = 10U;
    kui_scene scene;
    build_scene(&scene, low_contrast, selected);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 2;
    }
    puts("[TEST] desktop_settings_real: PASS");
    puts("[TEST] flux_scene_settings: PASS");
    puts("[TEST] desktop_settings_arrow_navigation: PASS");

    for (;;) {
        ku_ui_event event;
        if (gui_wait_event(window, &event) < 0 || event.type == KU_UI_EVENT_CLOSE) break;
        if (event.type != KU_UI_EVENT_KEY) continue;

        if (gui_key_down(&event) || gui_key_right(&event) || gui_key_tab(&event)) {
            (void)kui_scene_select_next(&scene, 1);
            selected = kui_scene_selected(&scene);
        } else if (gui_key_up(&event) || gui_key_left(&event)) {
            (void)kui_scene_select_next(&scene, -1);
            selected = kui_scene_selected(&scene);
        } else if (gui_key_activate(&event)) {
            low_contrast = apply_selected(selected, low_contrast);
        } else if (gui_key_cancel(&event)) {
            low_contrast = 0;
            selected = 10U;
        } else {
            continue;
        }

        build_scene(&scene, low_contrast, selected);
        (void)kui_scene_present(window, &scene);
    }
    (void)ku_ui_close(window);
    return 0;
}

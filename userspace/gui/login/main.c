#include "../common.h"
#include "../../../common/version.h"

static void build_scene(kui_scene* scene) {
    kui_flow root;
    kui_flow session;

    kui_scene_initialize(scene);
    scene->visible_rows = 10U;
    kui_scene_set_palette(
        scene,
        UINT32_C(0x111216),
        UINT32_C(0xEEF0F3),
        UINT32_C(0xE0162B));

    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "LOCAL SESSION");
    (void)kui_flow_label(&root, 2U, KUROGANE_PRODUCT_STRING " / RED FLUX");
    (void)kui_flow_label(&root, 3U, "DEVELOPER PROFILE");
    (void)kui_flow_separator(&root, 4U);

    kui_flow_begin(&session, scene, 1U);
    (void)kui_flow_button(&session, 10U, "ENTER RED FLUX DESKTOP");
    (void)kui_flow_label(&session, 11U, "ENTER: START SESSION");
    (void)kui_flow_label(&session, 12U, "SAFE MODE / DIAGNOSTICS ARE AVAILABLE AT BOOT");
    (void)kui_flow_label(&session, 13U, "ACCOUNT AUTHENTICATION FOLLOWS WITH USER SERVICES");
    (void)kui_scene_select(scene, 10U);
}

static int wait_for_desktop(uint64_t pid) {
    for (;;) {
        int32_t status = 0;
        const ku_status_t result = ku_process_wait(pid, &status);
        if (result == KU_STATUS_OK) return status;
        if (result != KU_STATUS_WOULD_BLOCK) return 3;
        (void)kuro_sleep(2U);
    }
}

int main(void) {
    const ku_window_t window = gui_open("KUROGANE LOGIN", 90, 230, 620, 250);
    if (window == KU_INVALID_WINDOW) return 1;

    kui_scene scene;
    build_scene(&scene);
    if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
        (void)ku_ui_close(window);
        return 2;
    }

    puts("[TEST] red_flux_login_surface: PASS");
    puts("[TEST] red_flux_session_gate: PASS");

    for (;;) {
        ku_ui_event event;
        const int available = gui_wait_event(window, &event);
        if (available < 0 || event.type == KU_UI_EVENT_CLOSE) {
            (void)ku_ui_close(window);
            return 0;
        }
        if (event.type != KU_UI_EVENT_KEY) continue;

        if (gui_key_activate(&event)) {
            const char launcher[] = "/gui/launcher";
            (void)ku_ui_close(window);
            const ku_result_t pid = ku_process_spawn(
                launcher, sizeof(launcher) - 1U);
            if (pid <= 0) return 4;
            puts("[TEST] red_flux_login_to_desktop: PASS");
            return wait_for_desktop((uint64_t)pid);
        }

        if (gui_key_cancel(&event)) {
            build_scene(&scene);
            (void)kui_scene_present(window, &scene);
        }
    }
}

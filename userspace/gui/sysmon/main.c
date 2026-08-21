#include "../common.h"

static void build_scene(kui_scene* scene, uint32_t heartbeat) {
    kui_flow root;
    kui_flow metrics;
    char pid[24];
    char tid[24];
    char identity[64] = "PID ";

    gui_u64(pid, sizeof(pid), ku_process_id());
    gui_u64(tid, sizeof(tid), ku_thread_id());
    gui_append_text(identity, sizeof(identity), pid);
    gui_append_text(identity, sizeof(identity), " / TID ");
    gui_append_text(identity, sizeof(identity), tid);

    kui_scene_initialize(scene);
    scene->visible_rows = KU_UI_MAX_LINES;
    gui_apply_obsidian_theme(scene, 0);
    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel_icon(
        &root, 1U, "SYSTEM MONITOR / RUNTIME",
        KU_ICON_APPLICATION_SYSTEM_MONITOR);
    (void)kui_flow_label_icon(&root, 2U, identity, KU_ICON_SPECIAL_KERNEL);
    (void)kui_flow_separator(&root, 3U);

    kui_flow_begin(&metrics, scene, 1U);
    (void)kui_flow_label_icon(
        &metrics, 10U, "SCHEDULER / ACTIVE", KU_ICON_STATUS_SUCCESS);
    (void)kui_flow_label_icon(
        &metrics, 11U, "RING 3 ISOLATION / ACTIVE", KU_ICON_SPECIAL_SHIELD);
    (void)kui_flow_progress_icon(
        &metrics, 12U, "SESSION HEARTBEAT", heartbeat, 100U,
        KU_ICON_STATUS_ONLINE);
    (void)kui_flow_label_icon(
        &metrics, 13U, "PROCESS LIFECYCLE / ONLINE", KU_ICON_STATUS_ONLINE);
    (void)kui_flow_label_icon(
        &metrics, 14U, "FORGED STEEL DESKTOP / HEALTHY", KU_ICON_STATUS_SUCCESS);
}

int main(void) {
    const ku_window_t window = gui_open("SYSTEM MONITOR", 315, 170, 560, 350);
    if (window == KU_INVALID_WINDOW) return 1;
    puts("[TEST] desktop_sysmon_ring3: PASS");
    puts("[TEST] kurogane5_obsidian_sysmon: PASS");

    uint32_t heartbeat = 0U;
    int scene_reported = 0;
    kui_scene scene;
    for (;;) {
        build_scene(&scene, heartbeat);
        if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
            (void)ku_ui_close(window);
            return 2;
        }
        if (!scene_reported) {
            puts("[TEST] flux_scene_sysmon: PASS");
            puts("[TEST] red_flux_sysmon: PASS");
            scene_reported = 1;
        }

        ku_ui_event event;
        const int available = kui_next_event(window, &event);
        if (available < 0 ||
            (available > 0 && event.type == KU_UI_EVENT_CLOSE)) {
            (void)ku_ui_close(window);
            return 0;
        }

        if (kuro_sleep_seconds(UINT64_C(1)) != KU_STATUS_OK) {
            (void)ku_ui_close(window);
            return 3;
        }
        heartbeat = (heartbeat + 5U) % 101U;
    }
}

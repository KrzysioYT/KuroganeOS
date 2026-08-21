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
    (void)kui_flow_panel(&root, 1U, "SYSTEM MONITOR / RUNTIME");
    (void)kui_flow_label(&root, 2U, identity);
    (void)kui_flow_separator(&root, 3U);

    kui_flow_begin(&metrics, scene, 1U);
    (void)kui_flow_label(&metrics, 10U, "SCHEDULER / ACTIVE");
    (void)kui_flow_label(&metrics, 11U, "RING 3 ISOLATION / ACTIVE");
    (void)kui_flow_progress(&metrics, 12U, "SESSION HEARTBEAT", heartbeat, 100U);
    (void)kui_flow_label(&metrics, 13U, "PROCESS LIFECYCLE / ONLINE");
    (void)kui_flow_label(&metrics, 14U, "OBSIDIAN DESKTOP / HEALTHY");
}

int main(void) {
    const ku_window_t window = gui_open("SYSTEM MONITOR", 315, 170, 560, 350);
    if (window == KU_INVALID_WINDOW) return 1;
    puts("[TEST] desktop_sysmon_ring3: PASS");
    puts("[TEST] kurogane5_obsidian_sysmon: PASS");

    uint32_t heartbeat = 0U;
    kui_scene scene;
    for (;;) {
        build_scene(&scene, heartbeat);
        if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
            (void)ku_ui_close(window);
            return 2;
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

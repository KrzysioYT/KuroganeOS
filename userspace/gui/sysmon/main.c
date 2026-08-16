#include "../common.h"

static void build_scene(kui_scene* scene, uint32_t heartbeat) {
    kui_flow root;
    kui_flow metrics;
    char pid[24];
    char tid[24];
    char identity[64] = "PID ";

    gui_u64(pid, sizeof(pid), ku_process_id());
    gui_u64(tid, sizeof(tid), ku_thread_id());
    (void)strlcpy(identity + strlen(identity), pid, sizeof(identity) - strlen(identity));
    (void)strlcpy(identity + strlen(identity), " // TID ", sizeof(identity) - strlen(identity));
    (void)strlcpy(identity + strlen(identity), tid, sizeof(identity) - strlen(identity));

    kui_scene_initialize(scene);
    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "SYSTEM MONITOR // USERSPACE");
    (void)kui_flow_label(&root, 2U, identity);
    (void)kui_flow_separator(&root, 3U);

    kui_flow_begin(&metrics, scene, 1U);
    (void)kui_flow_label(&metrics, 10U, "Scheduler heartbeat is active.");
    (void)kui_flow_label(&metrics, 11U, "Processes are isolated Ring 3 ELF64 images.");
    (void)kui_flow_progress(&metrics, 12U, "SCHEDULER HEARTBEAT", heartbeat, 100U);
    (void)kui_flow_label(&metrics, 13U, "Process/memory snapshots arrive with 2.6 capabilities.");
}

int main(void) {
    const ku_window_t window = gui_open("SYSTEM MONITOR", 30, 65, 470, 300);
    if (window == KU_INVALID_WINDOW) return 1;
    puts("[TEST] desktop_sysmon_ring3: PASS");

    uint32_t heartbeat = 0U;
    kui_scene scene;
    for (;;) {
        build_scene(&scene, heartbeat);
        if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
            (void)ku_ui_close(window);
            return 2;
        }
        if (heartbeat == 0U) puts("[TEST] flux_scene_sysmon: PASS");

        for (uint32_t tick = 0U; tick < 100U; ++tick) {
            ku_ui_event event;
            const int available = kui_next_event(window, &event);
            if (available < 0 || (available > 0 && event.type == KU_UI_EVENT_CLOSE)) {
                (void)ku_ui_close(window);
                return 0;
            }
            (void)kuro_sleep(1U);
        }
        heartbeat = (heartbeat + 5U) % 101U;
    }
}

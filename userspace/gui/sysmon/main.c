#include "../common.h"
#include <kurogane/flux_app_catalog.h>

static void build_scene(ku_window_t window, kui_scene* scene, const ku_system_snapshot* snapshot) {
    kui_flow root;
    kui_flow metrics;
    char pid[24];
    char tid[24];
    char identity[64] = "PID ";

    gui_u64(pid, sizeof(pid), ku_process_id());
    gui_u64(tid, sizeof(tid), ku_thread_id());
    (void)strlcpy(identity + strlen(identity), pid, sizeof(identity) - strlen(identity));
    (void)strlcpy(identity + strlen(identity), " / TID ", sizeof(identity) - strlen(identity));
    (void)strlcpy(identity + strlen(identity), tid, sizeof(identity) - strlen(identity));

    kui_scene_initialize(scene);
    kui_flow_begin(&root, scene, 0U);
    (void)kui_flow_panel(&root, 1U, "SYSTEM MONITOR / RUNTIME");
    if (snapshot != NULL) {
        char uptime[64] = "UPTIME / ";
        char seconds[24];
        gui_u64(seconds, sizeof(seconds), ku_system_uptime_seconds(snapshot));
        (void)strlcpy(uptime + strlen(uptime), seconds, sizeof(uptime) - strlen(uptime));
        (void)strlcpy(uptime + strlen(uptime), " S", sizeof(uptime) - strlen(uptime));
        (void)kui_flow_label(&root, 4U, uptime);
    } else {
        (void)kui_flow_label(&root, 4U, "SYSTEM SNAPSHOT / UNAVAILABLE");
    }
    (void)kui_flow_label(&root, 2U, identity);

    kui_flow_begin(&metrics, scene, 1U);
    (void)kui_flow_label(&metrics, 10U, "SCHEDULER / ACTIVE");
    (void)kui_flow_label(&metrics, 11U, "RING 3 ISOLATION / ACTIVE");
    (void)gui_update_scene_viewport(window, scene);
}

int main(void) {
    const ku_window_t window = gui_open(KU_FLUX_TITLE_MONITOR, 315, 190, 520, 320);
    if (window == KU_INVALID_WINDOW) return 1;
    puts("[TEST] desktop_sysmon_ring3: PASS");

    int scene_reported = 0;
    kui_scene scene;
    for (;;) {
        ku_system_snapshot snapshot = {0};
        snapshot.structure_size = sizeof(snapshot);
        const int valid = ku_system_get_snapshot(&snapshot) == KU_STATUS_OK &&
            snapshot.version == KU_SYSTEM_SNAPSHOT_VERSION;
        build_scene(window, &scene, valid ? &snapshot : NULL);
        if (kui_scene_present(window, &scene) != KU_STATUS_OK) {
            (void)ku_ui_close(window);
            return 2;
        }
        if (!scene_reported) {
            puts("[TEST] flux_scene_sysmon: PASS");
            puts("[TEST] red_flux_sysmon: PASS");
            if (valid) puts("[TEST] flux_sysmon_snapshot: PASS");
            scene_reported = 1;
        }

        ku_ui_event event;
        const int available = kui_next_event(window, &event);
        if (available < 0 ||
            (available > 0 && event.type == KU_UI_EVENT_CLOSE)) {
            (void)ku_ui_close(window);
            return 0;
        }

        /* Sample once a second instead of polling the scheduler every tick. */
        if (kuro_sleep_seconds(UINT64_C(1)) != KU_STATUS_OK) {
            (void)ku_ui_close(window);
            return 3;
        }
    }
}
